/* Host-side regression test for the GP2X 940 -> local sound fallback in
 * PsndRerate().
 *
 * This compiles the real pico/sound/sound.c with __GP2X__ and replaces the
 * hardware-facing 940 API and the sound cores with recording stubs.  It does
 * not execute ARM940 code, so it tests the 920-side fallback policy only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pico/pico_int.h"
#include "pico/sound/ym2612.h"
#include "pico/sound/ym2413.h"
#include "pico/sound/resampler.h"
#include "pico/cd/megasd.h"
#include "platform/common/ogg.h"

/* Normally provided by the emulator. */
PicoInterface PicoIn;
struct Pico Pico;
mcd_state *Pico_mcd;
struct megasd Pico_msd;
YM2612 ym2612;
OPLL *opll;

static int calls_ym2612_940;
static int calls_ym2612_local;
static int calls_ym2413_940;
static int calls_disable;
static int calls_release;
static int calls_save3;
static int calls_load3;
static int calls_opll_reset;
static int calls_opll_setrate;
static int calls_resampler_new;
static int calls_sn_init;
static int last_local_rate;
static int fail_ym2612_940;
static int ym2612_940_clears_opt;
static int fail_ym2413_940;

#define CHECK(x) do { \
  if (!(x)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

/* --- mocked 940 control layer ------------------------------------------ */

void ExtFmRelease_940(void) { calls_release++; }

int ExtFmStateIsLocal_940(void) { return 0; }

void ExtFmDisable_940(void)
{
  calls_disable++;
  PicoIn.opt &= ~POPT_EXT_FM;
}

int YM2612Init_940(int baseclock, int rate, int ssg)
{
  (void)baseclock; (void)rate; (void)ssg;
  calls_ym2612_940++;
  if (ym2612_940_clears_opt)
    PicoIn.opt &= ~POPT_EXT_FM;
  return fail_ym2612_940 ? -1 : 0;
}

int YM2413Init_940(int clock, int rate, int preserve_state)
{
  (void)clock; (void)rate; (void)preserve_state;
  calls_ym2413_940++;
  return fail_ym2413_940 ? -1 : 0;
}

/* --- mocked local sound cores and mixer -------------------------------- */

void YM2612Init_(int baseclock, int rate, int flags)
{
  (void)baseclock; (void)flags;
  calls_ym2612_local++;
  last_local_rate = rate;
}

size_t YM2612PicoStateSave3(void *buf, size_t size)
{
  calls_save3++;
  if (size > 8)
    size = 8;
  memset(buf, 0x5a, size);
  return size;
}

void YM2612PicoStateLoad3(const void *buf, size_t size)
{
  (void)buf; (void)size;
  calls_load3++;
}

int YM2612UpdateOne_(s32 *buffer, int length, int stereo, int is_buf_empty)
{
  (void)buffer; (void)length; (void)stereo; (void)is_buf_empty;
  return 0;
}

int YM2612UpdateOne_940(int *buffer, int length, int stereo, int is_buf_empty)
{
  (void)buffer; (void)length; (void)stereo; (void)is_buf_empty;
  return 0;
}

void YM2413Render_940(int *buffer, int length) { (void)buffer; (void)length; }

OPLL *OPLL_new(uint32_t clk, uint32_t rate) { (void)clk; (void)rate; return opll; }
void OPLL_delete(OPLL *o) { (void)o; }
void OPLL_reset(OPLL *o) { (void)o; calls_opll_reset++; }
void OPLL_setRate(OPLL *o, uint32_t rate) { o->rate = rate; calls_opll_setrate++; }
void OPLL_setChipType(OPLL *o, uint8_t type) { (void)o; (void)type; }
int16_t OPLL_calc(OPLL *o) { (void)o; return 0; }

resampler_t *resampler_new(unsigned taps, unsigned interpolation,
  unsigned decimation, double cutoff, double beta, unsigned max_input,
  int stereo)
{
  (void)taps; (void)interpolation; (void)decimation; (void)cutoff;
  (void)beta; (void)max_input; (void)stereo;
  calls_resampler_new++;
  return calloc(1, sizeof(resampler_t));
}

void resampler_free(resampler_t *r) { free(r); }

void resampler_update(resampler_t *r, s32 *buffer, int length,
  void (*generate_samples)(s32 *buffer, int length, int stereo))
{
  (void)r; (void)buffer; (void)length; (void)generate_samples;
}

int SN76496_init(int clock, int sample_rate)
{
  (void)clock; (void)sample_rate;
  calls_sn_init++;
  return 0;
}

void SN76496_set_clockrate(int clock, int sample_rate)
{
  (void)clock; (void)sample_rate;
  calls_sn_init++;
}

void SN76496Update(short *buffer, int length, int stereo)
{
  (void)buffer; (void)length; (void)stereo;
}

void mix_reset(int alpha_q16) { (void)alpha_q16; }
void mix_16h_to_32(s32 *dest, s16 *src, int count) { (void)dest; (void)src; (void)count; }
void mix_16h_to_32_s1(s32 *dest, s16 *src, int count) { (void)dest; (void)src; (void)count; }
void mix_16h_to_32_s2(s32 *dest, s16 *src, int count) { (void)dest; (void)src; (void)count; }
void mix_16h_to_32_resample_stereo(s32 *dest, s16 *src, int count, int fac16)
{ (void)dest; (void)src; (void)count; (void)fac16; }
void mix_16h_to_32_resample_mono(s32 *dest, s16 *src, int count, int fac16)
{ (void)dest; (void)src; (void)count; (void)fac16; }
void mix_32_to_16_stereo(s16 *dest, s32 *src, int count) { (void)dest; (void)src; (void)count; }
void mix_32_to_16_mono(s16 *dest, s32 *src, int count) { (void)dest; (void)src; (void)count; }

void memset32(void *dest, int c, int count)
{
  int i;
  for (i = 0; i < count; i++)
    ((u32 *)dest)[i] = (u32)c;
}

/* --- remaining emulator stubs ------------------------------------------ */

void PicoReratePico(void) {}
void PicoPicoPCMUpdate(short *buffer, int length, int stereo)
{ (void)buffer; (void)length; (void)stereo; }
void pcd_pcm_update(s32 *buffer, int length, int stereo)
{ (void)buffer; (void)length; (void)stereo; }
void p32x_pwm_update(s32 *buf32, int length, int stereo)
{ (void)buf32; (void)length; (void)stereo; }

size_t pm_read_audio(void *ptr, size_t bytes, pm_file *stream)
{ (void)ptr; (void)bytes; (void)stream; return 0; }
int pm_seek(pm_file *stream, long offset, int whence)
{ (void)stream; (void)offset; (void)whence; return 0; }
void mp3_start_play(void *f, int pos) { (void)f; (void)pos; }
void mp3_update(s32 *buffer, int length, int stereo)
{ (void)buffer; (void)length; (void)stereo; }
void ogg_start_play(void *f, int sample_offset) { (void)f; (void)sample_offset; }
void ogg_stop_play(void) {}
void ogg_update(s32 *buffer, int length, int stereo)
{ (void)buffer; (void)length; (void)stereo; }

/* --- tests ------------------------------------------------------------- */

static void reset_test(void)
{
  memset(&PicoIn, 0, sizeof(PicoIn));
  memset(&Pico, 0, sizeof(Pico));
  PicoIn.sndRate = 44100;
  PicoIn.sndOut = NULL;
  Pico.m.frame_count = 0;

  /* Put sound.c's static rate/option state back into local/idle mode so the
   * tests do not depend on the order in which they are run. */
  PsndRerate(0);

  calls_ym2612_940 = calls_ym2612_local = calls_ym2413_940 = 0;
  calls_disable = calls_release = calls_save3 = calls_load3 = 0;
  calls_opll_reset = calls_opll_setrate = 0;
  calls_resampler_new = calls_sn_init = 0;
  last_local_rate = -1;
  fail_ym2612_940 = 1;
  fail_ym2413_940 = 1;
  ym2612_940_clears_opt = 0;

  if (!opll)
    opll = calloc(1, sizeof(*opll));
  opll->rate = 0;
}

static void test_md_boot_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 0;

  PsndRerate(0);

  CHECK(calls_ym2612_940 == 1);        /* one attempt */
  CHECK(calls_disable == 1);           /* then the option is cleared */
  CHECK(!(PicoIn.opt & POPT_EXT_FM));
  CHECK(calls_ym2612_local == 1);      /* normal local init runs once */
  CHECK(last_local_rate == 44100);     /* no FM FIR filter in this test */
  CHECK(calls_release == 1);           /* retry sees the local chip */
  CHECK(calls_save3 == 0);
  CHECK(calls_load3 == 0);
  CHECK(calls_sn_init == 1);
  printf("PASS md boot fallback\n");
}

static void test_md_internal_clear_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 0;
  ym2612_940_clears_opt = 1;  /* what the old init did internally */

  PsndRerate(0);

  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 1);      /* the old code stopped here */
  CHECK(calls_disable == 1);
  CHECK(!(PicoIn.opt & POPT_EXT_FM));
  CHECK(calls_release == 1);
  printf("PASS md internal-clear fallback\n");
}

static void test_md_midgame_state(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 100;

  PsndRerate(1);

  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_disable == 1);
  CHECK(calls_ym2612_local == 1);
  CHECK(calls_save3 == 1);             /* state saved once ... */
  CHECK(calls_load3 == 1);             /* ... and restored once */
  CHECK(calls_sn_init == 1);
  printf("PASS md mid-game state fallback\n");
}

static void test_sms_boot_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 0;

  PsndRerate(0);

  CHECK(calls_ym2413_940 == 1);
  CHECK(calls_disable == 1);
  CHECK(!(PicoIn.opt & POPT_EXT_FM));
  CHECK(calls_release == 1);
  CHECK(calls_opll_setrate >= 1);
  CHECK(calls_opll_reset >= 1);
  CHECK(calls_resampler_new >= 2);     /* 940 setup, then the local path */
  CHECK(calls_sn_init == 1);
  printf("PASS sms boot fallback\n");
}

static void test_sms_midgame_state(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 100;

  PsndRerate(1);

  CHECK(calls_ym2413_940 == 1);
  CHECK(calls_disable == 1);
  CHECK(calls_ym2612_local == 0);      /* SMS path never inits the MD chip */
  CHECK(calls_resampler_new >= 2);
  CHECK(calls_sn_init == 1);
  printf("PASS sms mid-game fallback\n");
}

int main(void)
{
  test_md_boot_fallback();
  test_md_internal_clear_fallback();
  test_md_midgame_state();
  test_sms_boot_fallback();
  test_sms_midgame_state();
  printf("all tests passed\n");
  return 0;
}
