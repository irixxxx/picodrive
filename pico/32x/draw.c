/*
 * PicoDrive
 * (C) notaz, 2009,2010
 * (C) irixxxx, 2019-2026
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include "../pico_int.h"

// NB: 32X officially doesn't support H32 mode. However, it does work since the
// cartridge slot carries the EDCLK signal which is always H40 clock and is used
// as video clock by the 32X. The H32 MD image is overlaid with the 320 px 32X
// image which has the same on-screen width. How the /YS signal on the cartridge
// slot (signalling the display of background color) is processed in this case
// is however unclear and might lead to glitches due to race conditions by the
// different video clocks for H32 and H40.

// NB: there is an offset of 3.25 pixels between MD and 32X layers in H32 mode.
#define H32_OFFSET	(int)(3.25)

// BGR555 to native conversion
#if defined(USE_BGR555)
#define PXCONV(t)   ((t)&(mr|mg|mb|mp))
#define PXPRIO      0x8000  // prio in MSB
#elif defined(USE_BGR565)
#define PXCONV(t)   (((t)&mr)  | (((t)&(mg|mb)) << 1) | (((t)&mp) >> 10))
#define PXPRIO      0x0020  // prio in LS green bit
#else // RGB565 
#define PXCONV(t)   ((((t)&mr) << 11) | (((t)&mg) << 1) | (((t)&(mp|mb)) >> 10))
#define PXPRIO      0x0020  // prio in LS green bit
#endif

int (*PicoScan32xBegin)(unsigned int num);
int (*PicoScan32xEnd)(unsigned int num);
int Pico32xDrawMode;

void *DrawLineDestBase32x;
int DrawLineDestIncrement32x;

void *DrawLineDest32x;

static void convert_pal555(int invert_prio)
{
  u32 *ps = (void *)Pico32xMem->pal;
  u32 *pd = (void *)Pico32xMem->pal_native;
  u32 mr = 0x001f001f; // masks for red, green, blue, prio
  u32 mg = 0x03e003e0;
  u32 mb = 0x7c007c00;
  u32 mp = 0x80008000;
  u32 inv = 0;
  int i;

  if (invert_prio)
    inv = 0x80008000;

  for (i = 0x100/2; i > 0; i--, ps++, pd++) {
    u32 t = *ps ^ inv;
    *pd = PXCONV(t);
  }

  Pico32x.dirty_pal = 0;
}

// direct color mode
#define do_line_dc(pd, p32x, pmd, inv, pmd_draw_code)             \
{                                                                 \
  const u16 mr = 0x001f;                                          \
  const u16 mg = 0x03e0;                                          \
  const u16 mb = 0x7c00;                                          \
  const u16 mp = 0x0000;                                          \
  unsigned short t;                                               \
  int i = 320;                                                    \
                                                                  \
  while (i > 0) {                                                 \
    for (; i > 0 && (*pmd & 0x3f) == mdbg; pd++, pmd++, i--) {    \
      t = *p32x++;                                                \
      *pd = PXCONV(t);                                            \
    }                                                             \
    for (; i > 0 && (*pmd & 0x3f) != mdbg; pd++, pmd++, i--) {    \
      t = *p32x++ ^ inv;                                          \
      if (t & 0x8000)                                             \
        *pd = PXCONV(t);                                          \
      else                                                        \
        pmd_draw_code;                                            \
    }                                                             \
  }                                                               \
}

// packed pixel mode
#define do_line_pp(pd, p32x, pmd, pmd_draw_code)                  \
{                                                                 \
  unsigned short t;                                               \
  int i = 320;                                                    \
  while (i > 0) {                                                 \
    for (; i > 0 && (*pmd & 0x3f) == mdbg; pd++, pmd++, i--) {    \
      t = pal[*(u8 *)(MEM_BE2((uintptr_t)(p32x++)))];             \
      *pd = t;                                                    \
    }                                                             \
    for (; i > 0 && (*pmd & 0x3f) != mdbg; pd++, pmd++, i--) {    \
      t = pal[*(u8 *)(MEM_BE2((uintptr_t)(p32x++)))];             \
      if (t & PXPRIO)                                             \
        *pd = t;                                                  \
      else                                                        \
        pmd_draw_code;                                            \
    }                                                             \
  }                                                               \
}

// run length mode
#define do_line_rl(pd, p32x, pmd, offs, pmd_draw_code)            \
{                                                                 \
  short len = offs, t;                                            \
  int i;                                                          \
  for (i = 320; i > 0; p32x++) {                                  \
    t = pal[*p32x & 0xff];                                        \
    len += (*p32x >> 8) + 1;                                      \
    for (; len > 0 && i > 0; len--, i--, pd++, pmd++) {           \
      if ((*pmd & 0x3f) == mdbg || (t & PXPRIO))                  \
        *pd = t;                                                  \
      else                                                        \
        pmd_draw_code;                                            \
    }                                                             \
  }                                                               \
}

#define MD_LAYER_CODE_H32 \
  *dst = dst[H32_OFFSET]

// this is almost never used (Wiz, vert sw scaling and menu bg gen only)
void FinalizeLine32xRGB555(int sh, int line, struct PicoEState *est)
{
  unsigned short *dst = est->DrawLineDest;
  unsigned short *pal = Pico32xMem->pal_native;
  unsigned char  *pmd = est->HighCol + 8;
  unsigned short *dram, *p32x;
  unsigned char   mdbg;
  int lines_sft_offs = 0;

  FinalizeLine555(sh, line, est);

  if ((Pico32x.vdp_regs[0] & P32XV_Mx) == 0 || // 32x blanking
      (Pico.video.debug_p & PVD_KILL_32X))
  {
    return;
  }

  dram = (void *)Pico32xMem->dram[Pico32x.vdp_regs[0x0a/2] & P32XV_FS];
  p32x = dram + dram[line];
  mdbg = Pico.video.reg[7] & 0x3f;

  // hw bug: shift flag doesn't work if line addr is xxFF
  if ((Pico32x.vdp_regs[2 / 2] & P32XV_SFT) && ((dram[line]+1) & 0xff))
    lines_sft_offs |= 1 << 8;
  if (!(Pico.video.reg[12] & 1)) // H32, mind layer offset bit
    lines_sft_offs |= 2 << 8;
  if (PicoIn.opt & POPT_H32_LAYER_32X) // H32 centering
    lines_sft_offs |= 4 << 8;

  if ((Pico32x.vdp_regs[0] & P32XV_Mx) == 2) { // Direct Color Mode
    int inv_bit = (Pico32x.vdp_regs[0] & P32XV_PRI) ? 0x8000 : 0;
    p32x += (lines_sft_offs >> 8) & 1;                          \
    if (lines_sft_offs & (2<<8)) {
      if (lines_sft_offs & (4<<8)) {
        pmd += H32_OFFSET;
        do_line_dc(dst, p32x, pmd, inv_bit, MD_LAYER_CODE_H32);
      } else {
        p32x -= H32_OFFSET;
        do_line_dc(dst, p32x, pmd, inv_bit,);
      }
    } else
      do_line_dc(dst, p32x, pmd, inv_bit,);
    return;
  }

  if (Pico32x.dirty_pal)
    convert_pal555(Pico32x.vdp_regs[0] & P32XV_PRI);

  if ((Pico32x.vdp_regs[0] & P32XV_Mx) == 1) { // Packed Pixel Mode
    unsigned char *p32xb = (void *)p32x;
    p32xb += (lines_sft_offs >> 8) & 1;                          \
    if (lines_sft_offs & (2<<8)) {
      if (lines_sft_offs & (4<<8)) {
        pmd += H32_OFFSET;
        do_line_pp(dst, p32xb, pmd, MD_LAYER_CODE_H32);
      } else {
        p32xb -= H32_OFFSET;
        do_line_pp(dst, p32xb, pmd,);
      }
    } else
      do_line_pp(dst, p32xb, pmd,);
  }
  else { // Run Length Mode
    if (lines_sft_offs & (2<<8)) {
      if (lines_sft_offs & (4<<8)) {
        pmd += H32_OFFSET;
        do_line_rl(dst, p32x, pmd, -((lines_sft_offs>>8)&1), MD_LAYER_CODE_H32);
      } else {
        do_line_rl(dst, p32x, pmd, H32_OFFSET - ((lines_sft_offs>>8) & 1), );
      }
    } else
      do_line_rl(dst, p32x, pmd, -((lines_sft_offs>>8) & 1),);
  }
}

#define MD_LAYER_CODE \
  *dst = palmd[*pmd]

#define PICOSCAN_PRE \
  PicoScan32xBegin(l + (lines_sft_offs & 0xff)); \
  dst = Pico.est.DrawLineDest; \

#define PICOSCAN_POST \
  PicoScan32xEnd(l + (lines_sft_offs & 0xff)); \
  Pico.est.DrawLineDest = (char *)Pico.est.DrawLineDest + DrawLineDestIncrement32x; \

#define make_do_loop(name, pre_code, post_code, md_code)        \
/* Direct Color Mode */                                         \
static void do_loop_dc##name(unsigned short *dst,               \
    unsigned short *dram, unsigned lines_sft_offs, int mdbg)    \
{                                                               \
  int inv_bit = (Pico32x.vdp_regs[0] & P32XV_PRI) ? 0x8000 : 0; \
  unsigned char  *pmd = Pico.est.Draw2FB +                      \
                          328 * (lines_sft_offs & 0xff) + 8;    \
  unsigned short *palmd = Pico.est.HighPal;                     \
  unsigned short *p32x, loffs;                                  \
  int lines = (lines_sft_offs >> 16) & 0xff;                    \
  int h32 = (lines_sft_offs & (2<<8)) ? H32_OFFSET : 0;         \
  int l;                                                        \
  if (lines_sft_offs & (4<<8)) pmd += h32, h32 = 0;             \
  (void)palmd;                                                  \
  for (l = 0; l < lines; l++, pmd += 8) {                       \
    pre_code;                                                   \
    loffs = dram[l + (lines_sft_offs >> 24)];                   \
    p32x = dram + loffs - h32;                                  \
    if ((u8)(loffs+1))                                          \
      p32x += (lines_sft_offs >> 8) & 1;                        \
    do_line_dc(dst, p32x, pmd, inv_bit, md_code);               \
    post_code;                                                  \
    dst += DrawLineDestIncrement32x/2 - 320;                    \
  }                                                             \
}                                                               \
                                                                \
/* Packed Pixel Mode */                                         \
static void do_loop_pp##name(unsigned short *dst,               \
    unsigned short *dram, unsigned lines_sft_offs, int mdbg)    \
{                                                               \
  unsigned short *pal = Pico32xMem->pal_native;                 \
  unsigned char  *pmd = Pico.est.Draw2FB +                      \
                          328 * (lines_sft_offs & 0xff) + 8;    \
  unsigned short *palmd = Pico.est.HighPal;                     \
  unsigned char  *p32x;                                         \
  unsigned short loffs;                                         \
  int lines = (lines_sft_offs >> 16) & 0xff;                    \
  int h32 = (lines_sft_offs & (2<<8)) ? H32_OFFSET : 0;         \
  int l;                                                        \
  if (lines_sft_offs & (4<<8)) pmd += h32, h32 = 0;             \
  (void)palmd;                                                  \
  for (l = 0; l < lines; l++, pmd += 8) {                       \
    pre_code;                                                   \
    loffs = dram[l + (lines_sft_offs >> 24)];                   \
    p32x = (u8 *)(dram + loffs) - h32;                          \
    if ((u8)(loffs+1))                                          \
      p32x += (lines_sft_offs >> 8) & 1;                        \
    do_line_pp(dst, p32x, pmd, md_code);                        \
    post_code;                                                  \
    dst += DrawLineDestIncrement32x/2 - 320;                    \
  }                                                             \
}                                                               \
                                                                \
/* Run Length Mode */                                           \
static void do_loop_rl##name(unsigned short *dst,               \
    unsigned short *dram, unsigned lines_sft_offs, int mdbg)    \
{                                                               \
  unsigned short *pal = Pico32xMem->pal_native;                 \
  unsigned char  *pmd = Pico.est.Draw2FB +                      \
                          328 * (lines_sft_offs & 0xff) + 8;    \
  unsigned short *palmd = Pico.est.HighPal;                     \
  unsigned short *p32x, loffs;                                  \
  int lines = (lines_sft_offs >> 16) & 0xff;                    \
  int h32 = (lines_sft_offs & (2<<8)) ? H32_OFFSET : 0;         \
  int l;                                                        \
  (void)palmd;                                                  \
  if (lines_sft_offs & (4<<8)) pmd += h32, h32 = 0;             \
  for (l = 0; l < lines; l++, pmd += 8) {                       \
    pre_code;                                                   \
    loffs = dram[l + (lines_sft_offs >> 24)];                   \
    p32x = dram + loffs;                                        \
    loffs = (lines_sft_offs & (1<<8)) && ((loffs+1) & 0xff);    \
    do_line_rl(dst, p32x, pmd, h32-loffs, md_code);             \
    post_code;                                                  \
    dst += DrawLineDestIncrement32x/2 - 320;                    \
  }                                                             \
}

#ifdef _ASM_32X_DRAW
#undef make_do_loop
#define make_do_loop(name, pre_code, post_code, md_code) \
extern void do_loop_dc##name(unsigned short *dst,        \
    unsigned short *dram, unsigned lines_offs, int mdbg);\
extern void do_loop_pp##name(unsigned short *dst,        \
    unsigned short *dram, unsigned lines_offs, int mdbg);\
extern void do_loop_rl##name(unsigned short *dst,        \
    unsigned short *dram, unsigned lines_offs, int mdbg);
#endif

make_do_loop(,,,)
make_do_loop(_md, , , MD_LAYER_CODE)
make_do_loop(_h32, , , MD_LAYER_CODE_H32)
make_do_loop(_scan, PICOSCAN_PRE, PICOSCAN_POST, )
make_do_loop(_scan_h32, PICOSCAN_PRE, PICOSCAN_POST, MD_LAYER_CODE_H32)
make_do_loop(_scan_md, PICOSCAN_PRE, PICOSCAN_POST, MD_LAYER_CODE)

typedef void (*do_loop_func)(unsigned short *dst, unsigned short *dram, unsigned lines, int mdbg);
enum { DO_LOOP, DO_LOOP_H32, DO_LOOP_MD, DO_LOOP_SCAN, DO_LOOP_H32_SCAN, DO_LOOP_MD_SCAN };

static const do_loop_func do_loop_dc_f[] = { do_loop_dc, do_loop_dc_h32, do_loop_dc_md, do_loop_dc_scan, do_loop_dc_scan_h32, do_loop_dc_scan_md };
static const do_loop_func do_loop_pp_f[] = { do_loop_pp, do_loop_pp_h32, do_loop_pp_md, do_loop_pp_scan, do_loop_pp_scan_h32, do_loop_pp_scan_md };
static const do_loop_func do_loop_rl_f[] = { do_loop_rl, do_loop_rl_h32, do_loop_rl_md, do_loop_rl_scan, do_loop_rl_scan_h32, do_loop_rl_scan_md };

void PicoDraw32xLayer(int offs, int lines, int md_bg)
{
  int have_scan = PicoScan32xBegin != NULL && PicoScan32xEnd != NULL;
  const do_loop_func *do_loop;
  unsigned short *dram;
  int lines_sft_offs;
  int which_func;

  offs += Pico32x.sync_line;

  Pico.est.DrawLineDest = DrawLineDest32x;
  Pico.est.DrawLineDestIncr = DrawLineDestIncrement32x;
  dram = Pico32xMem->dram[Pico32x.vdp_regs[0x0a/2] & P32XV_FS];

  if (Pico32xDrawMode == PDM32X_BOTH)
    PicoDrawUpdateHighPal();

  if ((Pico32x.vdp_regs[0] & P32XV_Mx) == 2)
  {
    // Direct Color Mode
    do_loop = do_loop_dc_f;
    goto do_it;
  }

  if (Pico32x.dirty_pal)
    convert_pal555(Pico32x.vdp_regs[0] & P32XV_PRI);

  if ((Pico32x.vdp_regs[0] & P32XV_Mx) == 1)
  {
    // Packed Pixel Mode
    do_loop = do_loop_pp_f;
  }
  else
  {
    // Run Length Mode
    do_loop = do_loop_rl_f;
  }

do_it:
  lines_sft_offs = (Pico32x.sync_line << 24) | (lines << 16) | offs;
  if ((Pico32x.vdp_regs[2 / 2] & P32XV_SFT))
    lines_sft_offs |= 1 << 8;
  if (!(Pico.video.reg[12] & 1)) // H32, mind layer offset bit
    lines_sft_offs |= 2 << 8;
  if (PicoIn.opt & POPT_H32_LAYER_32X) // H32 centering
    lines_sft_offs |= 4 << 8;

  // In 8bit modes MD+32X layers are merged together in 32X rendering, while in
  // 16bit mode the MD layer is directly created in the target buffer and the
  // 32X layer is overlaid onto that.
  if (Pico32xDrawMode == PDM32X_BOTH) // 8bit
    which_func = have_scan ? DO_LOOP_MD_SCAN : DO_LOOP_MD;
  else if ((lines_sft_offs & ((2|4)<<8)) == (2|4)<<8) // H32, 32X centered
    which_func = have_scan ? DO_LOOP_H32_SCAN : DO_LOOP_H32; // mind H32 offset
  else
    which_func = have_scan ? DO_LOOP_SCAN : DO_LOOP;

  do_loop[which_func](Pico.est.DrawLineDest, dram, lines_sft_offs, md_bg);

  if (DrawLineDest32x == Pico.est.DrawLineDest)
    Pico.est.DrawLineDest = (char *)Pico.est.DrawLineDest + lines * DrawLineDestIncrement32x;
  DrawLineDest32x = Pico.est.DrawLineDest;
}

// mostly unused, games tend to keep 32X layer on
void PicoDraw32xLayerMdOnly(int offs, int lines)
{
  int have_scan = PicoScan32xBegin != NULL && PicoScan32xEnd != NULL;
  unsigned short *dst = (void *)((char *)DrawLineDestBase32x + offs * DrawLineDestIncrement32x);
  unsigned char  *pmd = Pico.est.Draw2FB + 328 * offs + 8;
  unsigned short *pal = Pico.est.HighPal;
  int plen = 320;
  int l, p;

  PicoDrawUpdateHighPal();

  offs += Pico32x.sync_line;
  dst += Pico32x.sync_line * DrawLineDestIncrement32x;

  for (l = 0; l < lines; l++) {
    if (have_scan) {
      PicoScan32xBegin(l + offs);
      dst = (unsigned short *)Pico.est.DrawLineDest;
    }
    for (p = 0; p < plen; p += 4) {
      dst[p + 0] = pal[*pmd++];
      dst[p + 1] = pal[*pmd++];
      dst[p + 2] = pal[*pmd++];
      dst[p + 3] = pal[*pmd++];
    }
    dst = Pico.est.DrawLineDest = (char *)dst + DrawLineDestIncrement32x;
    pmd += 328 - plen;
    if (have_scan)
      PicoScan32xEnd(l + offs);
  }
}

void PicoDrawSetOutFormat32x(pdso_t which, int use_32x_line_mode)
{
  if (which == PDF_RGB555) {
    // CLUT pixels needed as well, for layer priority
    PicoDrawSetInternalBuf(Pico.est.Draw2FB, 328);
    PicoDrawSetOutBufMD(NULL, 0);
  } else {
    // store CLUT pixels, same layout as alt renderer
    PicoDrawSetInternalBuf(NULL, 0);
    PicoDrawSetOutBufMD(Pico.est.Draw2FB, 328);
  }

  if (use_32x_line_mode)
    // we'll draw via FinalizeLine32xRGB555 (rare)
    Pico32xDrawMode = PDM32X_OFF;
  else
    // in RGB555 mode the 32x layer is overlaid on the MD layer, in the other
    // modes 32x and MD layer are merged together by the 32x renderer
    Pico32xDrawMode = (which == PDF_RGB555) ? PDM32X_32X_ONLY : PDM32X_BOTH;
}

void PicoDrawSetOutBuf32X(void *dest, int increment)
{
  DrawLineDestBase32x = dest;
  DrawLineDestIncrement32x = increment;
  // in RGB555 mode this buffer is also used by the MD renderer
  if (Pico32xDrawMode != PDM32X_BOTH)
    PicoDrawSetOutBufMD(DrawLineDestBase32x, DrawLineDestIncrement32x);
}

// vim:shiftwidth=2:ts=2:expandtab
