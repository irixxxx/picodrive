/* Host-side C-layer test for the GP2X ARM940 sound job loop.
 *
 * This compiles the real platform/gp2x/code940/940.c together with the
 * real YM2612 and YM2413 cores.  The ARM940 startup, IRQ, cache and MMIO
 * helpers are replaced with stubs.  host_core_940.c redirects the fixed
 * shared-memory pointers to ordinary process memory, and Main940() is
 * stopped by longjmp()ing out of wait_get_job() after the test job
 * sequence.
 *
 * This validates the C job protocol, buffer handling and YM2413 state
 * packing.  It does not execute ARM code, test the real wait_get_job MMIO
 * path, cache coherency, IRQ timing, or 920/940 races.
 */

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
  JOB940_YM2413UPDATEONE,
  JOB940_YM2413PACKSTATE,
  JOB940_YM2413UNPACKSTATE,
};

#define CHECK(x) do { \
  if (!(x)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

/* --- ARM940 helper stubs ---------------------------------------------- */

int wait_get_job(int oldjob)
{
  (void)oldjob;
  if (job_idx >= (int)(sizeof(jobs) / sizeof(jobs[0])))
    longjmp(jb, 1);
  return jobs[job_idx++];
}

void spend_cycles(int c) { (void)c; }
void dcache_clean(void) {}
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
  ctl->length = 128;
  ctl->stereo = 1;

  ctl->ym2413_clk = 3579545;
  ctl->ym2413_rate = 49716;
  ctl->ym2413_len = 16;

  /* Empty YM2612 write queues. */
  ctl->writebuff0[0] = 0xffff;
  ctl->writebuff1[0] = 0xffff;

  /* OPLL address 0x30, then key-on data 0x10; 0xffff terminates. */
  ctl->ym2413_wbuff[0] = 0x0030;
  ctl->ym2413_wbuff[1] = 0x0110;
  ctl->ym2413_wbuff[2] = 0xffff;

  /* Check that the update functions run and stop at the requested
   * lengths. */
  data->ym_buffer[0] = 0x12345678;
  data->ym_buffer[256] = 0x12345678;
  data->ym2413_buffer[0] = 0x12345678;
  data->ym2413_buffer[16] = 0x12345678;

  if (setjmp(jb) == 0)
    Main940();

  printf("host-core: magic=%08x loopc=%d lastjob=%d state_len=%d\n",
         ctl->sh_magic, ctl->loopc, ctl->lastjob,
         ctl->ym2413_state_len);
  printf("host-core: host offsets mp3dec=%d ym_buffer=%d mp3_buffer=%d "
         "ym2413=%d\n",
         ctl->sh_off_mp3dec, ctl->sh_off_ym_buffer,
         ctl->sh_off_mp3_buffer, ctl->sh_off_ym2413_buffer);

  CHECK((uint32_t)ctl->sh_magic == (uint32_t)SHARED_LAYOUT_MAGIC);
  CHECK(ctl->sh_off_mp3dec == (int)offsetof(_940_data_t, mp3dec));
  CHECK(ctl->sh_off_ym_buffer == (int)offsetof(_940_data_t, ym_buffer));
  CHECK(ctl->sh_off_mp3_buffer == (int)offsetof(_940_data_t, mp3_buffer));
  CHECK(ctl->sh_off_ym2413_buffer ==
        (int)offsetof(_940_data_t, ym2413_buffer));

  CHECK(data->mp3dec != NULL);
  CHECK(ctl->lastjob == jobs[sizeof(jobs) / sizeof(jobs[0]) - 1]);
  CHECK(ctl->loopc == (int)(sizeof(jobs) / sizeof(jobs[0])));
  CHECK(ctl->ym2413_state_len > 0);
  CHECK(ctl->ym2413_state_len <= (int)sizeof(ctl->ym2413_state));

  CHECK(data->ym_buffer[0] != 0x12345678);
  CHECK(data->ym_buffer[256] == 0x12345678);
  CHECK(data->ym2413_buffer[0] != 0x12345678);
  CHECK(data->ym2413_buffer[16] == 0x12345678);

  printf("host-core: passed\n");
  return 0;
}
