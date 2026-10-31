/* Host test for the 940 job loop. Compiles the real 940.c and sound cores;
 * ARM startup/IRQ/cache/MMIO are stubbed, and Main940() exits via longjmp.
 * Checks the C job protocol, buffering and YM2413 state, not ARM timing or
 * 920/940 races. */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

#include "pico/pico_int.h"
#include "platform/gp2x/code940/940shared.h"

/* Provided by the emulator in the real build. */
struct Pico Pico;
/* EXTERNAL_YM2612: chip state lives in shared data via this pointer */
extern YM2612 *ym2612_940;
void lprintf(const char *fmt, ...) { (void)fmt; }

/* Provided by host_core_940.c. */
void Main940(void);
void code940_host_set_memory(_940_data_t *data, _940_ctl_t *ctl,
                             unsigned char *mp3);

static _940_data_t host_data;
static _940_ctl_t host_ctl;
static unsigned char host_mp3[0x400000];

static jmp_buf jb;
static int job_idx;

static const int jobs[] = {
  JOB940_INITALL,
  JOB940_YM2612UPDATEONE,
  JOB940_YM2413RESET,
  JOB940_YM2413UPDATEONE,   /* A: address 0x30, data 0x10 */
  JOB940_YM2413PACKSTATE,   /* snapshot 0: A */
  JOB940_YM2413UPDATEONE,   /* B: address 0x30, data 0x11 */
  JOB940_YM2413PACKSTATE,   /* snapshot 1: B */
  JOB940_YM2413UNPACKSTATE, /* back to A (snapshot 0 restored first) */
  JOB940_YM2413PACKSTATE,   /* snapshot 2: must equal 0 */
  JOB940_YM2413UPDATEONE,   /* zero length: apply 0x30 = 0x0f */
  JOB940_YM2413PACKSTATE,   /* snapshot 3 */
};

static unsigned char pack_snap[4][1024];
static int pack_snap_len[4];
static int pack_count;

#define CHECK(x) do { \
  if (!(x)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

/* --- ARM940 helper stubs ---------------------------------------------- */

/* The 920 fills the buffer the 940 is not reading, then flips the selector
 * before submitting the job. */
static void set_ym2413_writes(int len, UINT16 a, UINT16 b)
{
  UINT16 *wbuff = host_ctl.writebuffsel ? host_ctl.writebuff0
                                        : host_ctl.writebuff1;

  host_ctl.ym2413_len = len;
  wbuff[0] = a;
  wbuff[1] = b;
  wbuff[2] = 0xffff;
  host_ctl.writebuffsel ^= 1;
}

static void prepare_job(int idx)
{
  switch (idx) {
  case 3: /* first YM2413 update: change user instrument register 0x30 */
    set_ym2413_writes(16, 0x0030, 0x0110);
    break;
  case 5: /* change the same register while the chip is live */
    set_ym2413_writes(16, 0x0030, 0x0111);
    break;
  case 7: /* put snapshot 0 back for UNPACKSTATE */
    memcpy(host_ctl.ym2413_state, pack_snap[0], pack_snap_len[0]);
    host_ctl.ym2413_state_len = pack_snap_len[0];
    break;
  case 9: /* zero length update: writes must still be applied */
    set_ym2413_writes(0, 0x0030, 0x010f);
    break;
  }
}

int wait_get_job(int oldjob)
{
  int job;

  (void)oldjob;
  if (job_idx >= (int)(sizeof(jobs) / sizeof(jobs[0])))
    longjmp(jb, 1);

  prepare_job(job_idx);
  job = jobs[job_idx++];
  return job;
}

void spend_cycles(int c) { (void)c; }

void dcache_clean(void)
{
  /* snapshot packed YM2413 state for the round-trip check */
  if (host_ctl.lastjob == JOB940_YM2413PACKSTATE &&
      pack_count < (int)(sizeof(pack_snap) / sizeof(pack_snap[0])) &&
      host_ctl.ym2413_state_len > 0 &&
      host_ctl.ym2413_state_len <= (int)sizeof(pack_snap[0]))
  {
    pack_snap_len[pack_count] = host_ctl.ym2413_state_len;
    memcpy(pack_snap[pack_count], host_ctl.ym2413_state,
           host_ctl.ym2413_state_len);
    pack_count++;
  }
}

void dcache_clean_flush(void) {}
void drain_wb(void) {}

void set_if_not_changed(int *val, int oldval, int newval)
{
  /* The real version uses swp; the test is single-threaded. */
  if (*val == oldval)
    *val = newval;
}

void __malloc_init(void) {}

/* --- Helix MP3 stubs --------------------------------------------------- */

void *MP3InitDecoder(void) { return (void *)1; }
void MP3FreeDecoder(void *h) { (void)h; }
int MP3Decode(void *h, unsigned char **inbuf, int *bytesLeft,
              short *outbuf, int useSize)
{
  (void)h;
  (void)inbuf;
  (void)bytesLeft;
  (void)outbuf;
  (void)useSize;
  return 0;
}

/* --- misc host helpers ------------------------------------------------- */

void memset32(void *dest, int c, int count)
{
  int i;
  for (i = 0; i < count; i++)
    ((uint32_t *)dest)[i] = (uint32_t)c;
}

int main(void)
{
  _940_data_t *data = &host_data;
  _940_ctl_t *ctl = &host_ctl;

  code940_host_set_memory(data, ctl, host_mp3);

  /* One 50 Hz frame of YM2612 and a small YM2413 native-rate block. */
  ctl->baseclock = 7670453;
  ctl->rate = 44100;
  ctl->ym2612_flags = ST_SSG;
  ctl->length = 128;
  ctl->stereo = 1;

  ctl->ym2413_clk = 3579545;
  ctl->ym2413_rate = 49716;
  ctl->ym2413_len = 16;

  /* Empty YM2612 write queues. */
  ctl->writebuff0[0] = 0xffff;
  ctl->writebuff1[0] = 0xffff;

  /* update functions must stop at the requested lengths */
  data->ym_buffer[0] = 0x12345678;
  data->ym_buffer[256] = 0x12345678;
  data->ym2413_buffer[0] = 0x12345678;
  data->ym2413_buffer[16] = 0x12345678;

  if (setjmp(jb) == 0)
    Main940();

  printf("host-core: loopc=%d lastjob=%d state_len=%d\n",
         ctl->loopc, ctl->lastjob, ctl->ym2413_state_len);
  printf("host-core: buffers ym=%d+%d mp3=%d+%d ym2413=%d+%d\n",
         ctl->sh_ym_buffer.offset, ctl->sh_ym_buffer.length,
         ctl->sh_mp3_buffer.offset, ctl->sh_mp3_buffer.length,
         ctl->sh_ym2413_buffer.offset, ctl->sh_ym2413_buffer.length);

  CHECK(ctl->protocol == PICODRIVE_940_PROTOCOL);
  CHECK(ctl->sh_ym_buffer.offset == (int)offsetof(_940_data_t, ym_buffer));
  CHECK(ctl->sh_ym_buffer.length == (int)sizeof(data->ym_buffer));
  CHECK(ctl->sh_mp3_buffer.offset == (int)offsetof(_940_data_t, mp3_buffer));
  CHECK(ctl->sh_mp3_buffer.length == (int)sizeof(data->mp3_buffer));
  CHECK(ctl->sh_ym2413_buffer.offset ==
        (int)offsetof(_940_data_t, ym2413_buffer));
  CHECK(ctl->sh_ym2413_buffer.length == (int)sizeof(data->ym2413_buffer));

  CHECK(data->mp3dec != NULL);
  CHECK(ctl->lastjob == jobs[sizeof(jobs) / sizeof(jobs[0]) - 1]);
  CHECK(ctl->loopc == (int)(sizeof(jobs) / sizeof(jobs[0])));
  CHECK(ctl->ym2413_state_len > 0);
  CHECK(ctl->ym2413_state_len <= (int)sizeof(ctl->ym2413_state));

  /* INITALL must forward the published clock, rate and SSG/DAC flags */
  CHECK(ym2612_940 == &data->ym2612);
  CHECK(ym2612_940->OPN.ST.clock == ctl->baseclock);
  CHECK(ym2612_940->OPN.ST.rate == ctl->rate);
  CHECK(ym2612_940->OPN.ST.flags == (UINT8)ctl->ym2612_flags);

  /* A pack/unpack round-trip must reproduce the same packed state. */
  CHECK(pack_count == 4);
  CHECK(pack_snap_len[0] > 0);
  CHECK(pack_snap_len[0] == pack_snap_len[1]);
  CHECK(pack_snap_len[0] == pack_snap_len[2]);
  CHECK(pack_snap_len[0] == pack_snap_len[3]);
  CHECK(memcmp(pack_snap[0], pack_snap[2], pack_snap_len[0]) == 0);
  /* register 0x30: A=0x10, B=0x11, back to A, then 0x0f */
  CHECK(pack_snap[0][4 + 0x30] == 0x10);
  CHECK(pack_snap[1][4 + 0x30] == 0x11);
  CHECK(pack_snap[2][4 + 0x30] == 0x10);
  CHECK(pack_snap[3][4 + 0x30] == 0x0f);

  CHECK(data->ym_buffer[0] != 0x12345678);
  CHECK(data->ym_buffer[256] == 0x12345678);
  CHECK(data->ym2413_buffer[0] != 0x12345678);
  CHECK(data->ym2413_buffer[16] == 0x12345678);

  /* legacy pack carries every held key; PICOSTATELOAD2 restores slot_mask
   * and per-slot keys from the keyon field */
  {
    uint32_t keyon;
    uint32_t expected = (1u << 0) | (1u << 6) | (1u << 9) | (1u << 15);

    YM2612Write_(0, 0x28);
    YM2612Write_(1, 0x10);  /* ch0 SLOT1 */
    YM2612Write_(1, 0x21);  /* ch1 SLOT2 */
    YM2612Write_(1, 0x42);  /* ch2 SLOT3 */
    YM2612Write_(1, 0x84);  /* ch3 SLOT4 */

    /* make the packed addon look like a dead local chip: no CH keys */
    memset(ym2612_940->CH, 0, sizeof(ym2612_940->CH));
    ym2612_940->slot_mask = 0;
    YM2612PicoStateSave2(0, 0, 0);
    memcpy(&keyon, &ym2612_940->REGS[0x120], sizeof(keyon));
    CHECK(keyon == 0);

    /* a live save or hand-over supplies the keyon field */
    keyon = expected;
    memcpy(&ym2612_940->REGS[0x120], &keyon, sizeof(keyon));
    CHECK(YM2612PicoStateLoad2(NULL, NULL, NULL) == 0);
    CHECK(ym2612_940->slot_mask == expected);
    CHECK(ym2612_940->CH[0].SLOT[0].key == 1);
    CHECK(ym2612_940->CH[1].SLOT[2].key == 1);
    CHECK(ym2612_940->CH[2].SLOT[1].key == 1);
    CHECK(ym2612_940->CH[3].SLOT[3].key == 1);
  }

  printf("host-core: passed\n");
  return 0;
}
