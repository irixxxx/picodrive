/* Host regression test for the 940 rerate policy. Compiles the real
 * sound.c with stubs; checks core selection, no automatic fallback, and
 * manual 940/920 handover. */

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
static int calls_ym2612_update_local;
static int calls_ym2612_update_940;
static int calls_ym2413_940;
static int calls_ym2413_render_940;
static int calls_opll_calc;
static int calls_release;
static int calls_save3;
static int calls_load3;
static int calls_opll_reset;
static int calls_opll_setrate;
static int calls_resampler_new;
static int calls_sn_init;
static int last_local_rate;
static int fail_ym2612_940;
static int fail_ym2413_940;
static int last_ym2612_940_clock;
static int last_ym2612_940_rate;
static int last_ym2612_940_flags;
static int last_ym2413_940_preserve;

#define CHECK(x) do { \
  if (!(x)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

/* --- mocked 940 control layer ------------------------------------------ */

void ExtFmRelease_940(void)
{
  calls_release++;
}

int YM2612Init_940(int baseclock, int rate, int ssg)
{
  calls_ym2612_940++;
  last_ym2612_940_clock = baseclock;
  last_ym2612_940_rate = rate;
  last_ym2612_940_flags = ssg;
  if (fail_ym2612_940)
    return -1;
  return 0;
}

int YM2413Init_940(int clock, int rate, int preserve_state)
{
  (void)clock; (void)rate;
  calls_ym2413_940++;
  last_ym2413_940_preserve = preserve_state;
  if (fail_ym2413_940)
    return -1;
  return 0;
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
  calls_ym2612_update_local++;
  return 0;
}

int YM2612UpdateOne_940(int *buffer, int length, int stereo, int is_buf_empty)
{
  (void)buffer; (void)length; (void)stereo; (void)is_buf_empty;
  calls_ym2612_update_940++;
  return 0;
}

void YM2413Render_940(int *buffer, int length)
{
  (void)buffer; (void)length;
  calls_ym2413_render_940++;
}

OPLL *OPLL_new(uint32_t clk, uint32_t rate) { (void)clk; (void)rate; return opll; }
void OPLL_delete(OPLL *o) { (void)o; }
void OPLL_reset(OPLL *o) { (void)o; calls_opll_reset++; }
void OPLL_setRate(OPLL *o, uint32_t rate) { o->rate = rate; calls_opll_setrate++; }
void OPLL_setChipType(OPLL *o, uint8_t type) { (void)o; (void)type; }
int16_t OPLL_calc(OPLL *o) { (void)o; calls_opll_calc++; return 0; }

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
  (void)r;
  if (generate_samples)
    generate_samples(buffer, length, 0);
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

  /* reset sound.c's static state so tests do not depend on run order */
  PsndRerate(0);

  calls_ym2612_940 = calls_ym2612_local = calls_ym2413_940 = 0;
  calls_ym2612_update_local = calls_ym2612_update_940 = 0;
  calls_ym2413_render_940 = calls_opll_calc = 0;
  calls_release = calls_save3 = calls_load3 = 0;
  calls_opll_reset = calls_opll_setrate = 0;
  calls_resampler_new = calls_sn_init = 0;
  last_local_rate = -1;
  fail_ym2612_940 = 1;
  fail_ym2413_940 = 1;
  last_ym2612_940_clock = last_ym2612_940_rate = last_ym2612_940_flags = -1;
  last_ym2413_940_preserve = -1;

  if (!opll)
    opll = calloc(1, sizeof(*opll));
  opll->rate = 0;
}

static void run_fm_update(void)
{
  static s16 out[8192];

  memset(out, 0, sizeof(out));
  PicoIn.opt |= POPT_EN_FM;
  PicoIn.sndOut = out;
  PsndDoFM(20000);
}

static void test_md_boot_no_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 0;

  PsndRerate(0);

  CHECK(calls_ym2612_940 == 1);        /* one attempt */
  CHECK(calls_ym2612_local == 0);      /* no automatic local fallback */
  CHECK(calls_release == 0);           /* the option was not cleared */
  CHECK(calls_save3 == 0);
  CHECK(calls_load3 == 0);
  CHECK(PicoIn.opt & POPT_EXT_FM);     /* the saved option is kept */
  CHECK(calls_sn_init == 1);

  /* dispatch stays on the 940 path, which is silent after a crash */
  run_fm_update();
  CHECK(calls_ym2612_update_940 == 1);
  CHECK(calls_ym2612_update_local == 0);
  printf("PASS md boot no fallback\n");
}

static void test_md_midgame_no_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 100;

  PsndRerate(1);

  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 0);
  CHECK(calls_save3 == 0);             /* YM2612Init_940() packs its own */
  CHECK(calls_load3 == 0);
  CHECK(PicoIn.opt & POPT_EXT_FM);
  CHECK(calls_sn_init == 1);

  /* a real parameter change still leaves handover to YM2612Init_940() */
  PicoIn.sndRate = 22050;
  calls_ym2612_940 = 0;
  PsndRerate(1);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_save3 == 0);
  CHECK(calls_load3 == 0);

  run_fm_update();
  CHECK(calls_ym2612_update_940 == 1);
  CHECK(calls_ym2612_update_local == 0);
  printf("PASS md mid-game no fallback\n");
}

static void test_md_option_off_on(void)
{
  reset_test();
  fail_ym2612_940 = 0;
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 100;

  PsndRerate(0);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 0);
  CHECK(last_ym2612_940_clock > 0);
  CHECK(last_ym2612_940_rate == 44100);
  CHECK(last_ym2612_940_flags == ST_SSG); /* defaults, no DAC */

  run_fm_update();
  CHECK(calls_ym2612_update_940 == 1);
  CHECK(calls_ym2612_update_local == 0);

  /* turn the 940 off: the chip is handed back to the 920 */
  PicoIn.opt &= ~POPT_EXT_FM;
  PsndRerate(0);
  CHECK(calls_release == 1);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 1);

  /* option back on even with unchanged params: 940 takes local state */
  calls_ym2612_940 = calls_ym2612_local = 0;
  calls_ym2612_update_940 = calls_ym2612_update_local = 0;
  calls_release = 0;
  PicoIn.opt |= POPT_EXT_FM;
  PsndRerate(0);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 0); /* the 940 path owns the local shadow */

  run_fm_update();
  CHECK(calls_ym2612_update_940 == 1);
  CHECK(calls_ym2612_update_local == 0);
  printf("PASS md option off/on\n");
}

static void test_md_crash_recovery(void)
{
  reset_test();
  fail_ym2612_940 = 0;
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 100;

  PsndRerate(0);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 0);

  /* menu-exit rerate still calls 940 init to reload after a crash; the
   * cache avoids restarting an unchanged live setup. */
  calls_ym2612_940 = calls_ym2612_local = 0;
  calls_save3 = calls_load3 = 0;
  PsndRerate(1);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 0);
  CHECK(calls_save3 == 0);
  CHECK(calls_load3 == 0);
  printf("PASS md crash recovery\n");
}

static void test_md_failed_recovery_no_fallback(void)
{
  reset_test();
  fail_ym2612_940 = 0;
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = 0;
  Pico.m.frame_count = 100;

  /* start normally so the option-on transition is behind us */
  PsndRerate(0);
  CHECK(calls_ym2612_940 == 1);
  CHECK(calls_ym2612_local == 0);

  /* next menu-exit rerate reloads and rejects the image; no local retry,
   * so the 940 stays selected and silent. */
  fail_ym2612_940 = 1;
  calls_ym2612_940 = calls_ym2612_local = 0;
  calls_save3 = calls_load3 = 0;
  PsndRerate(1);

  CHECK(calls_ym2612_940 == 1);        /* exactly one attempt */
  CHECK(calls_ym2612_local == 0);
  CHECK(calls_save3 == 0);
  CHECK(calls_load3 == 0);
  CHECK(PicoIn.opt & POPT_EXT_FM);

  run_fm_update();
  CHECK(calls_ym2612_update_940 == 1);
  CHECK(calls_ym2612_update_local == 0);
  printf("PASS md failed recovery no fallback\n");
}

static void test_sms_boot_no_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 0;

  PsndRerate(0);

  CHECK(calls_ym2413_940 == 1);
  CHECK(calls_ym2612_local == 0);      /* SMS path never inits the MD chip */
  CHECK(calls_release == 0);
  CHECK(PicoIn.opt & POPT_EXT_FM);
  CHECK(calls_resampler_new == 1);
  CHECK(calls_sn_init == 1);

  /* render stays on the 940 wrapper, silent after the failed init */
  run_fm_update();
  CHECK(calls_ym2413_render_940 == 1);
  CHECK(calls_opll_calc == 0);
  printf("PASS sms boot no fallback\n");
}

static void test_sms_midgame_no_fallback(void)
{
  reset_test();
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 100;

  PsndRerate(1);

  CHECK(calls_ym2413_940 == 1);
  CHECK(last_ym2413_940_preserve == 1);
  CHECK(calls_ym2612_local == 0);
  CHECK(PicoIn.opt & POPT_EXT_FM);

  run_fm_update();
  CHECK(calls_ym2413_render_940 == 1);
  CHECK(calls_opll_calc == 0);
  printf("PASS sms mid-game no fallback\n");
}

static void test_sms_option_off_on(void)
{
  reset_test();
  fail_ym2413_940 = 0;
  PicoIn.opt = POPT_EXT_FM;
  PicoIn.AHW = PAHW_SMS;
  Pico.m.frame_count = 100;

  PsndRerate(0);
  CHECK(calls_ym2413_940 == 1);
  CHECK(last_ym2413_940_preserve == 0);

  run_fm_update();
  CHECK(calls_ym2413_render_940 == 1);
  CHECK(calls_opll_calc == 0);

  PicoIn.opt &= ~POPT_EXT_FM;
  PsndRerate(0);
  CHECK(calls_release == 1);
  CHECK(calls_ym2413_940 == 1);

  run_fm_update();
  CHECK(calls_ym2413_render_940 == 1);
  CHECK(calls_opll_calc > 0);          /* local OPLL renders now */

  calls_ym2413_940 = 0;
  calls_release = 0;
  PicoIn.opt |= POPT_EXT_FM;
  PsndRerate(1);
  CHECK(calls_ym2413_940 == 1);
  CHECK(last_ym2413_940_preserve == 1); /* state is handed over */
  printf("PASS sms option off/on\n");
}

int main(void)
{
  test_md_boot_no_fallback();
  test_md_midgame_no_fallback();
  test_md_option_off_on();
  test_md_crash_recovery();
  test_md_failed_recovery_no_fallback();
  test_sms_boot_no_fallback();
  test_sms_midgame_no_fallback();
  test_sms_option_off_on();
  printf("all tests passed\n");
  return 0;
}
