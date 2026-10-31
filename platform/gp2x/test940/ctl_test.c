/* Host test for the 920 half of the 940 sound driver. Includes the real
 * 940ctl.c; only hardware/emulator calls are stubbed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/pico_int.h"

/* give the driver's file-static shared pointers normal memory */
#define shared_mem  host_shared_mem
#define shared_data host_shared_data
#define shared_ctl  host_shared_ctl
#define mp3_mem     host_mp3_mem

#include "../940ctl.c"

#undef shared_mem
#undef shared_data
#undef shared_ctl
#undef mp3_mem

#define CHECK(x) do { \
  if (!(x)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

/* --- emulator globals -------------------------------------------------- */

PicoInterface PicoIn;
struct Pico Pico;
YM2612 ym2612;
OPLL *opll;
short cdda_out_buffer[2*1152];

currentConfig_t currentConfig;
int reset_timing;
int engineState;

void *g_screen_ptr;
static void flip2_stub(void) {}
void (*gp2x_video_flip2)(void) = flip2_stub;

/* --- hardware stubs ---------------------------------------------------- */

static unsigned short host_memregs[0x20000];
static unsigned int host_memregl[0x20000];

volatile unsigned short *memregs = host_memregs;
volatile unsigned int *memregl = host_memregl;
int memdev = -1;

static int reset940_calls;
static int spend_calls;
static int publish_layout_on_start;
static char fake_image_path[512];
/* crash reported inside spend_cycles() while wait_busy_940() is busy-waiting */
static int crash_during_wait;

void reset940(int yes, int bank)
{
  (void)bank;
  reset940_calls++;
  if (!yes && publish_layout_on_start) {
    host_shared_ctl->protocol = PICODRIVE_940_PROTOCOL;
    host_shared_ctl->sh_ym_buffer.offset =
      (int)offsetof(_940_data_t, ym_buffer);
    host_shared_ctl->sh_ym_buffer.length =
      (int)sizeof(((_940_data_t *)0)->ym_buffer);
    host_shared_ctl->sh_mp3_buffer.offset =
      (int)offsetof(_940_data_t, mp3_buffer);
    host_shared_ctl->sh_mp3_buffer.length =
      (int)sizeof(((_940_data_t *)0)->mp3_buffer);
    host_shared_ctl->sh_ym2413_buffer.offset =
      (int)offsetof(_940_data_t, ym2413_buffer);
    host_shared_ctl->sh_ym2413_buffer.length =
      (int)sizeof(((_940_data_t *)0)->ym2413_buffer);
  }
}
void pause940(int yes) { (void)yes; }
void spend_cycles(int c)
{
  (void)c;
  spend_calls++;
  /* a crash reported mid-wait flips crashed_940 and clears the busy bit */
  if (crash_during_wait) {
    crash_during_wait = 0;   /* one-shot: later waits must complete */
    crashed_940 = 1;
    host_memregs[0x3b46 >> 1] = 0;
    return;
  }
  /* writing 1 clears the pending bit; otherwise start_940() looks crashed */
  host_memregs[0x3b46 >> 1] = 0;
}

void text_out16(int x, int y, const char *text, ...)
{ (void)x; (void)y; (void)text; }
int in_menu_wait(int interesting, char *charcode, int autorep_delay_ms)
{ (void)interesting; (void)charcode; (void)autorep_delay_ms; return 0; }
void menu_update_msg(const char *msg) { (void)msg; }
void emu_make_path(char *buff, const char *end, int size)
{
  (void)end;
  snprintf(buff, size, "%s", fake_image_path);
}

void memset32(void *dest, int c, int count)
{
  int i;
  for (i = 0; i < count; i++)
    ((uint32_t *)dest)[i] = (uint32_t)c;
}

/* --- sound core stubs -------------------------------------------------- */

static int ym2612_init_calls;
static int ym2612_reset_calls;
static int ym2612_pack_calls;
static int ym2612_unpack_calls;
static int last_pack_from_940;
static UINT32 ym2612_pack_keyon;
static int ym2413_pack_calls;
static int ym2413_unpack_calls;

void *YM2612GetRegs(void) { return ym2612.REGS; }

void YM2612Init_(int baseclock, int rate, int flags)
{
  (void)baseclock; (void)rate; (void)flags;
  ym2612_init_calls++;
  /* the real YM2612Init_() starts from a cleared register file */
  memset(ym2612.REGS, 0, sizeof(ym2612.REGS));
  ym2612.addr_A1 = 0;
}

void YM2612ResetChip_(void)
{
  ym2612_reset_calls++;
  memset(ym2612.REGS, 0, sizeof(ym2612.REGS));
  ym2612.addr_A1 = 0;
}
void YM2612PicoStateLoad_(void) {}

/* the real timer helper lives in memory.c, which this test does not link */
void ym2612_pack_timer_vals(int *tat, int *tbt, int *busy)
{
  *tat = *tbt = *busy = 0;
}

/* local-chip pack; the real one calls YM2612PicoStateSave2() in ym2612.c */
void ym2612_pack_state_old(void)
{
  ym2612_pack_calls++;
  last_pack_from_940 = 0;
  /* the real pack writes the addon block, including the magic */
  memset(&ym2612.REGS[0x100], 0, 0x30);
  *(UINT32 *)&ym2612.REGS[0x100] = 0x41534d59;
  memcpy(&ym2612.REGS[0x120], &ym2612_pack_keyon, sizeof(ym2612_pack_keyon));
}
void ym2612_unpack_state_old(void)
{ ym2612_unpack_calls++; }

void OPLL_reset(OPLL *o) { (void)o; }
void OPLL_setRate(OPLL *o, uint32_t rate) { (void)o; (void)rate; }

size_t ym2413_pack_state(void *buf, size_t size)
{
  ym2413_pack_calls++;
  if (size > 8)
    size = 8;
  memset(buf, 0x3c, size);
  return size;
}

void ym2413_unpack_state(const void *buf, size_t size)
{
  (void)buf; (void)size;
  ym2413_unpack_calls++;
}

int _mp3dec_start(FILE *f, int fpos_start)
{ (void)f; (void)fpos_start; return 0; }
int _mp3dec_decode(FILE *f, int *file_pos, int file_len)
{ (void)f; (void)file_pos; (void)file_len; return 0; }

/* --- test setup -------------------------------------------------------- */

static _940_data_t host_data;
static _940_ctl_t host_ctl;

static void reset_test(void)
{
  memset(&host_data, 0, sizeof(host_data));
  memset(&host_ctl, 0, sizeof(host_ctl));
  memset(host_memregs, 0, sizeof(host_memregs));
  memset(host_memregl, 0, sizeof(host_memregl));
  memset(&ym2612, 0, sizeof(ym2612));
  internal_reset();

  host_shared_mem = NULL;
  host_mp3_mem = NULL;
  host_shared_data = &host_data;
  host_shared_ctl = &host_ctl;

  /* the descriptors the 940 publishes; the real image fills these in */
  host_shared_ctl->protocol = PICODRIVE_940_PROTOCOL;
  host_shared_ctl->sh_ym_buffer.offset =
    (int)offsetof(_940_data_t, ym_buffer);
  host_shared_ctl->sh_ym_buffer.length =
    (int)sizeof(host_data.ym_buffer);
  host_shared_ctl->sh_mp3_buffer.offset =
    (int)offsetof(_940_data_t, mp3_buffer);
  host_shared_ctl->sh_mp3_buffer.length =
    (int)sizeof(host_data.mp3_buffer);
  host_shared_ctl->sh_ym2413_buffer.offset =
    (int)offsetof(_940_data_t, ym2413_buffer);
  host_shared_ctl->sh_ym2413_buffer.length =
    (int)sizeof(host_data.ym2413_buffer);

  crashed_940 = 0;
  core940_owner = CORE940_NONE;
  reset_ym2612_940_cache();
  ym2413_writebuff_ptr = 0;
  ym2413_last_reg = -1;
  ym2413_job_pending = 0;
  ym2413_job_len = 0;
  ym2413_outcount = 0;

  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 0;

  reset940_calls = 0;
  spend_calls = 0;
  publish_layout_on_start = 0;
  crash_during_wait = 0;
  ym2612_init_calls = 0;
  ym2612_reset_calls = 0;
  ym2612_pack_calls = 0;
  ym2612_unpack_calls = 0;
  last_pack_from_940 = -1;
  ym2612_pack_keyon = 0;
  ym2413_pack_calls = 0;
  ym2413_unpack_calls = 0;

  if (!opll)
    opll = calloc(1, sizeof(*opll));
}

/* --- tests ------------------------------------------------------------- */

static void test_ym2612_init_cache(void)
{
  reset_test();
  core940_owner = CORE940_YM2612;
  Pico.m.frame_count = 10;
  ym2612_940_clock = 7670453;
  ym2612_940_rate = 44100;
  ym2612_940_flags = ST_SSG;

  /* unchanged setup: no restart */
  CHECK(YM2612Init_940(7670453, 44100, ST_SSG) == 0);
  CHECK(reset940_calls == 0);

  /* a changed clock/rate/flags restarts the 940; the missing test image
   * makes the layout check fail, which proves it */
  CHECK(YM2612Init_940(7670453 + 1, 44100, ST_SSG) < 0);
  CHECK(reset940_calls == 2); /* start_940() resets the core twice */

  reset_test();
  core940_owner = CORE940_YM2612;
  Pico.m.frame_count = 10;
  ym2612_940_clock = 7670453;
  ym2612_940_rate = 44100;
  ym2612_940_flags = ST_SSG;
  CHECK(YM2612Init_940(7670453, 22050, ST_SSG) < 0);
  CHECK(reset940_calls == 2);

  reset_test();
  core940_owner = CORE940_YM2612;
  Pico.m.frame_count = 10;
  ym2612_940_clock = 7670453;
  ym2612_940_rate = 44100;
  ym2612_940_flags = ST_SSG;
  CHECK(YM2612Init_940(7670453, 44100, ST_DAC) < 0);
  CHECK(reset940_calls == 2);

  printf("PASS ym2612 init cache\n");
}

static void test_ym2612_reset_cleanup(void)
{
  reset_test();
  core940_owner = CORE940_YM2612;
  Pico.m.frame_count = 10;
  currentConfig.EmuOpt = 4;

  YM2612Write_940(0, 0x30, 0);
  YM2612Write_940(1, 0x11, 0);
  CHECK(writebuff_ptr != 0);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0030);

  /* a pending update must be retired before the queue is discarded */
  host_memregs[0x3b46 >> 1] |= 1 << (JOB940_YM2612UPDATEONE - 1);
  spend_calls = 0;
  YM2612ResetChip_940();

  CHECK(spend_calls > 0);
  CHECK(writebuff_ptr == 0);
  CHECK(host_shared_ctl->writebuff0[0] == YM_WBUFF_END);
  CHECK(host_shared_ctl->writebuff1[0] == YM_WBUFF_END);
  CHECK(host_shared_ctl->writebuffsel == 0);
  CHECK(ym2612_reset_calls == 1);
  CHECK(ym2612.REGS[0] == 0);

  printf("PASS ym2612 reset cleanup\n");
}

static void test_ym2612_writes_while_muted(void)
{
  reset_test();
  core940_owner = CORE940_YM2612;
  Pico.m.frame_count = 10;
  currentConfig.EmuOpt = 0;   /* muted / fast-forward: sound disabled */

  YM2612Write_940(0, 0x30, 0);
  YM2612Write_940(1, 0x11, 0);

  /* writes are retained for the next render instead of being dropped */
  CHECK(writebuff_ptr != 0);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0030);
  CHECK(host_shared_ctl->writebuff1[1] == 0x0111);

  printf("PASS ym2612 writes while muted\n");
}

static void test_ym2612_queue_overflow(void)
{
  int i;

  reset_test();
  core940_owner = CORE940_YM2612;
  currentConfig.EmuOpt = 0;

  for (i = 0; i < YM2612_WBUFF_MAX - 1; i++)
    YM2612Write_940(1, i & 0xff, 0);

  CHECK(writebuff_ptr == YM2612_WBUFF_MAX - 1);
  CHECK(host_shared_ctl->writebuffsel == 0);
  CHECK(host_shared_ctl->writebuff1[YM2612_WBUFF_MAX - 2] ==
        ((1 << 8) | ((YM2612_WBUFF_MAX - 2) & 0xff)));

  /* overflow: apply the queue with a zero-length update, then start the
   * next buffer so the write is not lost */
  YM2612Write_940(1, 0x55, 0);
  CHECK(writebuff_ptr == 1);
  CHECK(host_shared_ctl->writebuff1[YM2612_WBUFF_MAX - 1] == YM_WBUFF_END);
  CHECK(host_shared_ctl->writebuff0[0] == 0x0155);
  CHECK(host_shared_ctl->writebuffsel == 1);
  CHECK(host_shared_ctl->length == 0);
  CHECK(host_memregs[(0x3B20 + (JOB940_YM2612UPDATEONE - 1) * 2) >> 1] == 1);

  printf("PASS ym2612 queue overflow\n");
}

static void test_ym2612_queue_full_sound_on(void)
{
  int i;

  reset_test();
  core940_owner = CORE940_YM2612;
  currentConfig.EmuOpt = EOPT_EN_SOUND;

  for (i = 0; i < YM2612_WBUFF_MAX - 1; i++)
    YM2612Write_940(1, i & 0xff, 0);

  /* sound is enabled, so rendering drains the queue every scanline; a full
   * buffer here is not a mute/FF overflow and must not trigger a flush */
  YM2612Write_940(1, 0x55, 0);
  CHECK(writebuff_ptr == YM2612_WBUFF_MAX - 1);
  CHECK(host_shared_ctl->writebuffsel == 0);
  CHECK(host_shared_ctl->length == 0);
  CHECK(host_memregs[(0x3B20 + (JOB940_YM2612UPDATEONE - 1) * 2) >> 1] == 0);

  printf("PASS ym2612 queue full with sound on\n");
}

static void test_ym2413_reg_filter(void)
{
  reset_test();
  core940_owner = CORE940_YM2413;

  YM2413_regWrite_940(0x20);
  YM2413_regWrite_940(0x20);   /* same address, no effect */
  YM2413_regWrite_940(0x21);
  YM2413_dataWrite_940(0x11);
  YM2413_regWrite_940(0x21);   /* address latch already at 0x21 */

  CHECK(ym2413_writebuff_ptr == 3);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0020);
  CHECK(host_shared_ctl->writebuff1[1] == 0x0021);
  CHECK(host_shared_ctl->writebuff1[2] == 0x0111);

  printf("PASS ym2413 register filter\n");
}

static void test_bad_protocol_rejected(void)
{
  reset_test();

  /* valid descriptors are not enough: an old image has no protocol field */
  host_shared_ctl->protocol = 0;
  CHECK(check_shared_layout_940() < 0);

  host_shared_ctl->protocol = PICODRIVE_940_PROTOCOL;
  CHECK(check_shared_layout_940() == 0);

  printf("PASS 940 protocol check\n");
}

static void test_failed_init_keeps_940(void)
{
  int out[32];
  int i;

  reset_test();
  PicoIn.AHW = 0;
  Pico.m.frame_count = 0;

  /* the image cannot publish a usable layout */
  CHECK(YM2612Init_940(7670453, 44100, ST_SSG) < 0);
  CHECK(crashed_940 == 1);
  CHECK(core940_owner == CORE940_YM2612);
  CHECK(PicoIn.opt & POPT_EXT_FM);      /* the session option is kept */
  CHECK(ym2612_init_calls == 1);         /* local chip is ready for option-off */

  for (i = 0; i < 16; i++)
    out[i] = 0x55555555;
  CHECK(YM2612UpdateOne_940(out, 16, 1, 1) == 0);
  for (i = 0; i < 16; i++)
    CHECK(out[i] == 0);                 /* failed 940 is silent */

  /* next menu exit retries; no local fallback, 940 stays owner */
  host_shared_mem = calloc(1, 0x200000);
  CHECK(host_shared_mem != NULL);
  {
    char saved_path[sizeof(fake_image_path)];

    snprintf(saved_path, sizeof(saved_path), "%s", fake_image_path);
    snprintf(fake_image_path, sizeof(fake_image_path), "/dev/null");
    reset940_calls = 0;
    CHECK(YM2612Init_940(7670453, 44100, ST_SSG) < 0);
    CHECK(reset940_calls == 2);         /* start_940 resets twice */
    CHECK(crashed_940 == 1);
    CHECK(core940_owner == CORE940_YM2612);
    CHECK(PicoIn.opt & POPT_EXT_FM);
    snprintf(fake_image_path, sizeof(fake_image_path), "%s", saved_path);
  }
  free(host_shared_mem);
  host_shared_mem = NULL;

  /* the YM2413 initializer follows the same policy */
  reset_test();
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 0;
  CHECK(YM2413Init_940(3579545, 49716, 0) < 0);
  CHECK(crashed_940 == 1);
  CHECK(core940_owner == CORE940_YM2413);
  CHECK(PicoIn.opt & POPT_EXT_FM);

  printf("PASS failed init keeps 940\n");
}

static void test_crash_silence(void)
{
  int out[32];
  int i;

  reset_test();
  crashed_940 = 1;

  for (i = 0; i < 32; i++)
    out[i] = 0x55555555;
  CHECK(YM2612UpdateOne_940(out, 16, 1, 1) == 0);
  for (i = 0; i < 32; i++)
    CHECK(out[i] == 0);

  for (i = 0; i < 32; i++)
    out[i] = 0x55555555;
  YM2413Render_940(out, 16);
  for (i = 0; i < 16; i++)
    CHECK(out[i] == 0);
  CHECK(out[16] == 0x55555555);

  /* a dead 940 is never polled */
  host_memregs[0x3b46 >> 1] = 0xffff;
  spend_calls = 0;
  wait_busy_940(JOB940_YM2612UPDATEONE);
  CHECK(spend_calls == 0);

  /* state loads and MP3 jobs are skipped after a crash */
  YM2612PicoStateLoad_940();
  CHECK(spend_calls == 0);
  {
    int tat = 1, tbt = 2;
    CHECK(YM2612PicoStateLoad2_940(&tat, &tbt) == -1);
  }
  CHECK(spend_calls == 0);
  CHECK(mp3dec_start(NULL, 0) == -1);

  memset(cdda_out_buffer, 0x55, sizeof(cdda_out_buffer));
  {
    int pos = 7;
    CHECK(mp3dec_decode(NULL, &pos, 999) == 1);
    CHECK(pos == 999);
  }
  for (i = 0; i < (int)(sizeof(cdda_out_buffer)/sizeof(cdda_out_buffer[0])); i++)
    CHECK(cdda_out_buffer[i] == 0);
}

/* A crash that lands mid-wait: the top-of-function check passes, but the
 * coprocessor dies before the stale shared buffer is copied out. The real
 * mp3 path must stop playback (file_pos == mp3_len) and leave cdda_out_buffer
 * silent. This guards the race between wait_busy_940 returning and the copy. */
static void test_mp3_crash_mid_wait(void)
{
  int i;

  reset_test();
  PicoIn.opt = POPT_EXT_FM;          /* reach the 940 mp3 decode path */
  PicoIn.AHW = PAHW_SMS;
  crashed_940 = 0;                   /* top-of-function check passes */

  /* preload the shared mp3 buffer with non-zero samples so the old path
   * that copied it unconditionally would leak them */
  {
    short *mp3buf = (short *)host_data.mp3_buffer;
    size_t j, n = sizeof(host_data.mp3_buffer) / sizeof(host_data.mp3_buffer[0]);
    for (j = 0; j < n; j++)
      mp3buf[j] = 0x7f7f;
  }

  host_shared_ctl->mp3_len = 999;
  host_shared_ctl->mp3_offs = 500;   /* playback still ongoing */
  host_shared_ctl->mp3_buffsel = 0;
  host_shared_ctl->sh_mp3_buffer.offset =
    (int)offsetof(_940_data_t, mp3_buffer);
  host_shared_ctl->sh_mp3_buffer.length =
    (int)sizeof(host_data.mp3_buffer);
  host_shared_ctl->protocol = PICODRIVE_940_PROTOCOL;
  loaded_mp3 = (void *)&cdda_out_buffer[0]; /* playback considered started */

  /* simulate the 940 dying during the busy wait: the busy bit makes wait_busy_940
   * run, and crash_during_wait flips crashed_940 inside spend_cycles, mid-wait */
  host_memregs[0x3b46 >> 1] = 1 << (JOB940_MP3DECODE - 1);
  crash_during_wait = 1;

  memset(cdda_out_buffer, 0x55, sizeof(cdda_out_buffer));
  {
    int pos = 7;
    CHECK(mp3dec_decode(NULL, &pos, 999) == 1);
    CHECK(pos == 999);               /* playback stopped */
  }
  for (i = 0; i < (int)(sizeof(cdda_out_buffer) / sizeof(cdda_out_buffer[0])); i++)
    CHECK(cdda_out_buffer[i] == 0);  /* stale buffer not copied */

  printf("PASS mp3 crash mid wait\n");
}

static void test_ym2612_save_crash(void)
{
  int reg = (0x3B20 + (JOB940_PICOSTATESAVE2 - 1) * 2) >> 1;

  reset_test();
  crashed_940 = 1;
  host_memregs[reg] = 0;

  /* dead core must not save; the caller would read stale data */
  YM2612PicoStateSave2_940(0, 0);
  CHECK(host_memregs[reg] == 0);

  printf("PASS ym2612 save crash\n");
}

static void test_ym2612_release_after_crash(void)
{
  reset_test();
  core940_owner = CORE940_YM2612;
  Pico.m.frame_count = 10;
  crashed_940 = 1;

  ym2612.REGS[0x40] = 0x12;
  ym2612_pack_keyon = 0xffff;

  /* option off after a crash: no salvage, just a clean local chip */
  ExtFmRelease_940();
  CHECK(core940_owner == CORE940_NONE);
  CHECK(ym2612_pack_calls == 0);
  CHECK(ym2612_unpack_calls == 0);
  CHECK(ym2612_reset_calls == 1);
  CHECK(ym2612.REGS[0x40] == 0);

  /* redundant menu-exit call does nothing */
  ExtFmRelease_940();
  CHECK(ym2612_reset_calls == 1);

  printf("PASS ym2612 release after crash\n");
}

static void test_handover_keeps_key_state(void)
{
  reset_test();
  core940_owner = CORE940_NONE;   /* local chip is authoritative */
  Pico.m.frame_count = 10;
  crashed_940 = 0;
  publish_layout_on_start = 1;
  host_shared_mem = calloc(1, 0x200000);
  CHECK(host_shared_mem != NULL);

  /* local play left two keys set; the handover pack carries them */
  ym2612_pack_keyon = 0x0042;
  CHECK(YM2612Init_940(7670453, 44100, ST_SSG|ST_DAC) == 0);
  CHECK(ym2612_pack_calls == 1);
  CHECK(last_pack_from_940 == 0);
  CHECK(*(UINT32 *)&ym2612.REGS[0x120] == 0x0042);

  free(host_shared_mem);
  host_shared_mem = NULL;
  printf("PASS handover keeps key state\n");
}

static void test_ym2612_first_enable_handover(void)
{
  reset_test();
  PicoIn.AHW = 0;
  Pico.m.frame_count = 10;
  core940_owner = CORE940_NONE;
  crashed_940 = 1;              /* boot: the image has not been loaded yet */
  publish_layout_on_start = 1;
  host_shared_mem = calloc(1, 0x200000);
  CHECK(host_shared_mem != NULL);

  ym2612_pack_keyon = 0x0042;
  CHECK(YM2612Init_940(7670453, 44100, ST_SSG|ST_DAC) == 0);
  CHECK(crashed_940 == 0);
  CHECK(core940_owner == CORE940_YM2612);
  CHECK(ym2612_pack_calls == 1);
  CHECK(last_pack_from_940 == 0);
  CHECK(*(UINT32 *)&ym2612.REGS[0x120] == 0x0042);

  free(host_shared_mem);
  host_shared_mem = NULL;
  printf("PASS ym2612 first enable handover\n");
}

static void test_ym2413_first_enable_handover(void)
{
  reset_test();
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 10;
  core940_owner = CORE940_NONE;
  crashed_940 = 1;              /* boot: the image has not been loaded yet */
  publish_layout_on_start = 1;
  host_shared_mem = calloc(1, 0x200000);
  CHECK(host_shared_mem != NULL);

  CHECK(YM2413Init_940(3579545, 49716, 0) == 0);
  CHECK(crashed_940 == 0);
  CHECK(core940_owner == CORE940_YM2413);
  CHECK(ym2413_pack_calls == 1);
  CHECK(host_shared_ctl->ym2413_state_len == 8);
  CHECK(host_shared_ctl->ym2413_state[0] == 0x3c);

  free(host_shared_mem);
  host_shared_mem = NULL;
  printf("PASS ym2413 first enable handover\n");
}

static void test_ym2413_reenable_after_crash(void)
{
  reset_test();
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 10;
  core940_owner = CORE940_NONE;
  crashed_940 = 1;              /* the crashed 940 was handed back */
  publish_layout_on_start = 1;
  host_shared_mem = calloc(1, 0x200000);
  CHECK(host_shared_mem != NULL);

  CHECK(YM2413Init_940(3579545, 49716, 0) == 0);
  CHECK(crashed_940 == 0);
  CHECK(core940_owner == CORE940_YM2413);
  CHECK(ym2413_pack_calls == 1);
  CHECK(host_shared_ctl->ym2413_state_len == 8);
  CHECK(host_shared_ctl->ym2413_state[0] == 0x3c);

  free(host_shared_mem);
  host_shared_mem = NULL;
  printf("PASS ym2413 reenable after crash\n");
}

static void test_ym2612_crash_during_handover(void)
{
  int reg = (0x3B20 + (JOB940_PICOSTATESAVE2 - 1) * 2) >> 1;

  reset_test();
  PicoIn.AHW = 0;
  Pico.m.frame_count = 10;
  core940_owner = CORE940_YM2612;
  crashed_940 = 0;
  /* the 940 dies while answering the save: the job is still pending when
   * wait_busy_940() blocks on it, and spend_cycles() reports the crash */
  host_memregs[0x3b46 >> 1] |= 1 << (JOB940_PICOSTATESAVE2 - 1);
  crash_during_wait = 1;
  ym2612.REGS[0x40] = 0x12;
  *(UINT32 *)&ym2612.REGS[0x120] = 0xffff;
  publish_layout_on_start = 1;
  host_shared_mem = calloc(1, 0x200000);
  CHECK(host_shared_mem != NULL);

  /* the 940 dies during the pack: restart fresh, do not keep half state */
  CHECK(YM2612Init_940(7670453, 44100, ST_SSG|ST_DAC) == 0);
  CHECK(crashed_940 == 0);
  CHECK(host_memregs[reg] == 1);      /* the 940 save was requested */
  CHECK(spend_calls > 0);             /* ...and the wait saw it die */
  CHECK(ym2612_unpack_calls == 0);
  CHECK(ym2612.REGS[0x40] == 0);
  CHECK(*(UINT32 *)&ym2612.REGS[0x120] == 0);

  free(host_shared_mem);
  host_shared_mem = NULL;
  printf("PASS ym2612 crash during handover\n");
}

static void test_render_blocks(void)
{
  int out[2 * YM2413_BUF_MAX + 16];
  int i;

  reset_test();
  for (i = 0; i < YM2413_BUF_MAX; i++)
    host_data.ym2413_buffer[i] = i + 1;
  for (i = 0; i < (int)(sizeof(out) / sizeof(out[0])); i++)
    out[i] = -1;

  YM2413_regWrite_940(0x30);
  YM2413_dataWrite_940(0x10);
  CHECK(ym2413_writebuff_ptr == 2);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0030);
  CHECK(host_shared_ctl->writebuff1[1] == 0x0110);

  YM2413Render_940(out, YM2413_BUF_MAX + 100);

  /* second block reuses the 940 buffer; correct loop copies from its start */
  CHECK(out[0] == 1);
  CHECK(out[YM2413_BUF_MAX - 1] == YM2413_BUF_MAX);
  CHECK(out[YM2413_BUF_MAX] == 1);
  CHECK(out[YM2413_BUF_MAX + 99] == 100);
  CHECK(out[YM2413_BUF_MAX + 100] == -1);

  /* the writes were consumed by the first synchronous block, then the next
   * frame was submitted asynchronously */
  CHECK(ym2413_writebuff_ptr == 0);
  CHECK(ym2413_job_pending == 1);
  CHECK(ym2413_job_len == YM2413_BUF_MAX);
  CHECK(host_shared_ctl->ym2413_len == YM2413_BUF_MAX);
  CHECK(host_shared_ctl->writebuff1[0] == YM_WBUFF_END);

  printf("PASS ym2413 render blocks\n");
}

static void test_ym2413_async(void)
{
  int out[64];
  int i;

  reset_test();
  for (i = 0; i < (int)(sizeof(out) / sizeof(out[0])); i++)
    host_data.ym2413_buffer[i] = i + 1;

  /* first call primes the one-frame pipeline and submits the next job */
  YM2413Render_940(out, 32);
  CHECK(out[0] == 1);
  CHECK(out[31] == 32);
  CHECK(ym2413_job_pending == 1);
  CHECK(ym2413_job_len == 32);
  CHECK(host_shared_ctl->ym2413_len == 32);

  /* a write while the job is in flight goes into the other buffer */
  YM2413_regWrite_940(0x42);
  CHECK(ym2413_writebuff_ptr == 1);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0042);
  CHECK(host_shared_ctl->writebuff0[0] == YM_WBUFF_END);

  /* the 940 finishes while the 920 emulates the next frame */
  host_memregs[0x3b46 >> 1] |= 1 << (JOB940_YM2413UPDATEONE - 1);
  host_memregs[0x3b46 >> 1] = 0;
  for (i = 0; i < (int)(sizeof(out) / sizeof(out[0])); i++)
    host_data.ym2413_buffer[i] = 1000 + i;

  spend_calls = 0;
  YM2413Render_940(out, 32);
  CHECK(spend_calls == 0); /* completed job is collected without blocking */
  CHECK(out[0] == 1000);
  CHECK(out[31] == 1031);
  CHECK(ym2413_job_pending == 1);
  CHECK(host_shared_ctl->ym2413_len == 32);
  CHECK(ym2413_writebuff_ptr == 0);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0042);
  CHECK(host_shared_ctl->writebuff1[1] == YM_WBUFF_END);

  printf("PASS ym2413 async pipeline\n");
}

static void test_ym2413_async_save(void)
{
  int out[32];
  int i;

  reset_test();
  host_shared_ctl->ym2413_state_len = 4;
  memset(host_shared_ctl->ym2413_state, 0x5a, 4);
  for (i = 0; i < 32; i++)
    host_data.ym2413_buffer[i] = i + 1;

  YM2413Render_940(out, 32);
  CHECK(ym2413_job_pending == 1);

  YM2413_regWrite_940(0x55);
  CHECK(ym2413_writebuff_ptr == 1);

  /* state save retires the in-flight job and applies the queued write, but
   * keeps the already rendered samples for playback */
  CHECK(YM2413PicoStateSave_940(out, sizeof(out)) == 4);
  CHECK(ym2413_outcount == 32);
  CHECK(ym2413_writebuff_ptr == 0);
  CHECK(ym2413_job_pending == 0);
  CHECK(host_shared_ctl->ym2413_state[0] == 0x5a);

  memset(out, -1, sizeof(out));
  YM2413Render_940(out, 32);
  CHECK(out[0] == 1);
  CHECK(out[31] == 32);
  CHECK(ym2413_job_pending == 1);

  printf("PASS ym2413 async state save\n");
}

static void test_queue_overflow(void)
{
  int i;

  reset_test();
  for (i = 0; i < YM2413_WBUFF_MAX - 1; i++)
    YM2413_regWrite_940(0x10 + (i & 0x0f));
  CHECK(ym2413_writebuff_ptr == YM2413_WBUFF_MAX - 1);

  /* overflow: flush with a zero-length update, then queue this write in the
   * next buffer */
  YM2413_dataWrite_940(0x55);
  CHECK(ym2413_writebuff_ptr == 1);
  CHECK(host_shared_ctl->writebuff0[0] == 0x0155);
  CHECK(host_shared_ctl->writebuff1[0] == 0x0010);
  CHECK(host_shared_ctl->writebuff1[YM2413_WBUFF_MAX - 1] == YM_WBUFF_END);
  CHECK(host_shared_ctl->ym2413_len == 0);

  printf("PASS ym2413 queue overflow\n");
}

static void test_ym2413_reset(void)
{
  reset_test();
  core940_owner = CORE940_YM2413;
  ym2413_clock = 3579545;
  ym2413_rate = 49716;

  /* same clock/rate, no state preservation: reset in place, keep the 940 */
  ym2413_writebuff_ptr = 3;
  CHECK(YM2413Init_940(3579545, 49716, 0) == 0);
  CHECK(reset940_calls == 0);
  CHECK(ym2413_writebuff_ptr == 0);
  CHECK(host_shared_ctl->writebuff0[0] == YM_WBUFF_END);
  CHECK(host_shared_ctl->writebuff1[0] == YM_WBUFF_END);
  CHECK(host_shared_ctl->writebuffsel == 0);

  /* preserve_state keeps pending writes for the next frame */
  ym2413_writebuff_ptr = 5;
  CHECK(YM2413Init_940(3579545, 49716, 1) == 0);
  CHECK(reset940_calls == 0);
  CHECK(ym2413_writebuff_ptr == 5);

  printf("PASS ym2413 in-place reset\n");
}

static void test_ym2413_restart_packs_940(void)
{
  reset_test();
  core940_owner = CORE940_YM2413;
  ym2413_clock = 3579545;
  ym2413_rate = 49716;
  Pico.m.frame_count = 10;

  host_shared_ctl->ym2413_state_len = 4;
  memset(host_shared_ctl->ym2413_state, 0x5a, 4);

  YM2413_regWrite_940(0x30);
  YM2413_dataWrite_940(0x10);
  CHECK(ym2413_writebuff_ptr == 2);

  /* clock change restarts the 940: apply the queue and pack live state,
   * not the local shadow. The restart fails without an image, but must not
   * restore into the local OPLL or select the local path. */
  CHECK(YM2413Init_940(3579545, 44100, 0) < 0);
  CHECK(reset940_calls == 2);
  CHECK(crashed_940 == 1);
  CHECK(core940_owner == CORE940_YM2413);
  CHECK(ym2413_pack_calls == 0);
  CHECK(ym2413_writebuff_ptr == 0);
  CHECK(ym2413_unpack_calls == 0);

  printf("PASS ym2413 restart handover\n");
}

static void test_release_resets_queue(void)
{
  reset_test();
  core940_owner = CORE940_YM2413;
  ym2413_writebuff_ptr = 4;
  host_shared_ctl->ym2413_state_len = 0;

  ExtFmRelease_940();

  CHECK(core940_owner == CORE940_NONE);
  CHECK(ym2413_writebuff_ptr == 0);
  CHECK(ym2413_unpack_calls == 0); /* empty state: nothing to restore */

  printf("PASS ym2413 release\n");
}

static void test_ym2612_release_cache(void)
{
  reset_test();
  core940_owner = CORE940_YM2612;
  ym2612_940_clock = 7670453;
  ym2612_940_rate = 44100;
  ym2612_940_flags = 0;

  /* a healthy 940 answers the save with its packed register block */
  *(UINT32 *)((UINT8 *)host_shared_ctl->writebuff0 + 0x100) = 0x41534d59;

  ExtFmRelease_940();

  CHECK(core940_owner == CORE940_NONE);
  CHECK(ym2612_940_clock == -1);
  CHECK(ym2612_940_rate == -1);
  CHECK(ym2612_940_flags == -1);
  CHECK(*(UINT32 *)&ym2612.REGS[0x100] == 0x41534d59); /* 940 pack was used */
  CHECK(ym2612_unpack_calls == 1);

  printf("PASS ym2612 release cache\n");
}

static void test_sms_ownership(void)
{
  reset_test();
  core940_owner = CORE940_YM2413;

  CHECK(YM2413_940_is_active());

  PicoIn.AHW = PAHW_MCD | PAHW_VGM;
  CHECK(!YM2413_940_is_active());

  PicoIn.AHW = PAHW_SMS;
  PicoIn.opt &= ~POPT_EXT_FM;
  CHECK(!YM2413_940_is_active());

  printf("PASS sms ownership guard\n");
}

static void setup_fake_image(void)
{
  const char *slash = strrchr(__FILE__, '/');
  int dirlen = slash ? (int)(slash - __FILE__ + 1) : 0;
  FILE *f;

  snprintf(fake_image_path, sizeof(fake_image_path),
           "%.*s%s", dirlen, __FILE__, "build/fake940.bin");
  f = fopen(fake_image_path, "wb");
  CHECK(f != NULL);
  fputs("fake 940 image", f);
  fclose(f);
}

int main(void)
{
  setup_fake_image();
  test_ym2612_init_cache();
  test_ym2612_reset_cleanup();
  test_ym2612_writes_while_muted();
  test_ym2612_queue_overflow();
  test_ym2612_queue_full_sound_on();
  test_ym2413_reg_filter();
  test_bad_protocol_rejected();
  test_failed_init_keeps_940();
  test_crash_silence();
  test_mp3_crash_mid_wait();
  test_ym2612_save_crash();
  test_ym2612_release_after_crash();
  test_handover_keeps_key_state();
  test_ym2612_first_enable_handover();
  test_ym2413_first_enable_handover();
  test_ym2413_reenable_after_crash();
  test_ym2612_crash_during_handover();
  test_render_blocks();
  test_ym2413_async();
  test_ym2413_async_save();
  test_queue_overflow();
  test_ym2413_reset();
  test_ym2413_restart_packs_940();
  test_release_resets_queue();
  test_ym2612_release_cache();
  test_sms_ownership();
  printf("all ctl tests passed\n");
  return 0;
}
