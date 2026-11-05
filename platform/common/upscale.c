/*
 * upscale.c		image upscaling
 *
 * This file contains upscalers for picodrive.
 *
 * scaler types:
 * nn:	nearest neighbour
 * snn:	"smoothed" nearest neighbour (see below)
 * bln:	n-level-bilinear with n quantized weights
 *	quantization: 0: a<1/(2*n), 1/n: 1/(2*n)<=a<3/(2*n), etc
 *	currently n=2, n=4 are implemented (there's n=8 mixing, but no filters)
 *	[NB this has been brought to my attn, which is probably the same as bl2:
 *	https://www.drdobbs.com/image-scaling-with-bresenham/184405045?pgno=1]
 *
 * "smoothed" nearest neighbour: uses the average of the source pixels if no
 *	source pixel covers more than 65% of the result pixel. It definitely
 *	looks better than nearest neighbour and is still quite fast. It creates
 *	a sharper look than a bilinear filter, at the price of some visible jags
 *	on diagonal edges.
 * 
 * example scaling modes:
 * 256x_Y_ -> 320x_Y_, H32/mode 4, PAR 5:4, for PAL DAR 4:3 (NTSC 7% aspect err)
 * 256x224 -> 320x240, H32/mode 4, PAR 5:4, for NTSC DAR 4:3 (PAL 7% aspect err)
 * 320x224 -> 320x240, PAR 1:1, for NTSC, DAR 4:3 (PAL 7% etc etc...)
 * 160x144 -> 320x240: GG, PAR 6:5, scaling to 320x240 for DAR 4:3
 * 
 * (C) 2021 irixxxx
 *
 * This work is licensed under the terms of any of these licenses
 * (at your option):
 *  - GNU GPL, version 2 or later.
 *  - MAME license.
 */

#include <stdlib.h>
#include <string.h>
#include "upscale.h"

/* X x Y -> X*5/4 x Y */
void upscale_clut_nn_x_4_5(u8 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_nn_4_5(di, ds, si, ss, width, f_nop);
	}
}

void upscale_rgb_nn_x_4_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_nn_4_5(di, ds, si, ss, width, f_pal);
	}
}

void upscale_rgb_snn_x_4_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_snn_4_5(di, ds, si, ss, width, f_pal);
	}
}

void upscale_rgb_bl2_x_4_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_bl2_4_5(di, ds, si, ss, width, f_pal);
	}
}

void upscale_rgb_bl4_x_4_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_bl4_4_5(di, ds, si, ss, width, f_pal);
	}
}

/* X x Y -> X*5/4 x Y*17/16 */
void upscale_clut_nn_x_4_5_y_16_17(u8 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 8; j++) {
			h_upscale_nn_4_5(di, ds, si, ss, width, f_nop);
		}
		di +=  ds;
		for (j = 0; j < 8; j++) {
			h_upscale_nn_4_5(di, ds, si, ss, width, f_nop);
		}

		di -= 9*ds;
		v_copy(&di[0], &di[-ds], swidth, f_nop);
		di += 9*ds;
	}
}

void upscale_rgb_nn_x_4_5_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 8; j++) {
			h_upscale_nn_4_5(di, ds, si, ss, width, f_pal);
		}
		di +=  ds;
		for (j = 0; j < 8; j++) {
			h_upscale_nn_4_5(di, ds, si, ss, width, f_pal);
		}

		di -= 9*ds;
		v_copy(&di[0], &di[-ds], swidth, f_nop);
		di += 9*ds;
	}
}

void upscale_rgb_snn_x_4_5_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 8; j++) {
			h_upscale_snn_4_5(di, ds, si, ss, width, f_pal);
		}
		di +=  ds;
		for (j = 0; j < 8; j++) {
			h_upscale_snn_4_5(di, ds, si, ss, width, f_pal);
		}

		/* mix lines 6-8 */
		di -= 9*ds;
		v_mix(&di[0], &di[-ds], &di[ds], swidth, p_05, f_nop);
		v_mix(&di[-ds], &di[-2*ds], &di[-ds], swidth, p_05, f_nop);
		v_mix(&di[ ds], &di[ ds], &di[ 2*ds], swidth, p_05, f_nop);
		di += 9*ds;
	}
}

void upscale_rgb_bl2_x_4_5_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 4; j++) {
			h_upscale_bl2_4_5(di, ds, si, ss, width, f_pal);
		}
		di +=  ds;
		for (j = 0; j < 12; j++) {
			h_upscale_bl2_4_5(di, ds, si, ss, width, f_pal);
		}
		/* mix lines 3-10 */
		di -= 13*ds;
			v_mix(&di[0], &di[-ds], &di[ds], swidth, p_05, f_nop);
		for (j = 0; j < 7; j++) {
			di += ds;
			v_mix(&di[0], &di[0], &di[ds], swidth, p_05, f_nop);
		}
		di += 6*ds;
	}
}

void upscale_rgb_bl4_x_4_5_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 2; j++) {
			h_upscale_bl4_4_5(di, ds, si, ss, width, f_pal);
		}
		di += ds;
		for (j = 0; j < 14; j++) {
			h_upscale_bl4_4_5(di, ds, si, ss, width, f_pal);
		}
		di -= 15*ds;
		/* mixing line 2: line 1 = -ds, line 2 = +ds */
			v_mix(&di[0], &di[-ds], &di[ds], swidth, p_025, f_nop);
			di += ds;
		/* mixing lines 3-5: line n-1 = 0, line n = +ds */
		for (j = 0; j < 3; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_025, f_nop);
			di += ds;
			}
		/* mixing lines 6-9 */
		for (j = 0; j < 4; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_05, f_nop);
			di += ds;
		}
		/* mixing lines 10-13 */
		for (j = 0; j < 4; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_075, f_nop);
			di += ds;
		}
		/* lines 14-16, already in place */
		di += 3*ds;
	}
}

/* "classic" upscaler as found in several emulators. It's really more like a
 * x*4/3, y*16/15 upscaler, with an additional 5th row/17th line just inserted
 * from the source image. That gives nice n/4,n/16 alpha values plus better
 * symmetry in each block and avoids "borrowing" a row/line between blocks.
 */
void upscale_rgb_bln_x_4_5_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 4; j++) {
			h_upscale_bln_4_5(di, ds, si, ss, width, f_pal);
		}
		di += ds;
		for (j = 0; j < 12; j++) {
			h_upscale_bln_4_5(di, ds, si, ss, width, f_pal);
		}
		di -= 13*ds;
		/* mixing line 4: line 3 = -ds, line 4 = +ds */
			v_mix(&di[0], &di[-ds], &di[ds], swidth, p_025, f_nop);
			di += ds;
		/* mixing lines 5-6: line n-1 = 0, line n = +ds */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_025, f_nop);
			di += ds;
			}
		/* mixing line 7-9 */
		for (j = 0; j < 3; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_05, f_nop);
			di += ds;
		}
		/* mixing lines 10-12 */
		for (j = 0; j < 3; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_075, f_nop);
			di += ds;
		}
		/* lines 13-16, already in place */
		di += 4*ds;
	}
}

/* experimental 8 level bilinear for quality assessment */
void upscale_rgb_bl8_x_4_5_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 5/4;
	int y, j;

	for (y = 0; y < 224; y += 16) {
		for (j = 0; j < 2; j++) {
			h_upscale_bl8_4_5(di, ds, si, ss, width, f_pal);
		}
		di += ds;
		for (j = 0; j < 14; j++) {
			h_upscale_bl8_4_5(di, ds, si, ss, width, f_pal);
		}
		di -= 15*ds;
		/* mixing line 2: line 2 = -ds, line 3 = +ds */
			v_mix(&di[0], &di[-ds], &di[ds], swidth, p_0125, f_nop);
			di += ds;
		/* mixing line 3: line 3 = 0, line 4 = +ds */
			v_mix(&di[0], &di[0], &di[ds], swidth, p_0125, f_nop);
			di += ds;
		/* mixing lines 4-5: line n-1 = 0, line n = +ds */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], swidth, p_025, f_nop);
			di += ds;
			}
		/* mixing lines 6-7 */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], 320, p_0375, f_nop);
			di += ds;
		}
		/* mixing lines 8-9 */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], 320, p_05, f_nop);
			di += ds;
		}
		/* mixing lines 10-11 */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], 320, p_0625, f_nop);
			di += ds;
		}
		/* mixing lines 12-13 */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], 320, p_075, f_nop);
			di += ds;
		}
		/* mixing lines 14-15 */
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[0], &di[ds], 320, p_0875, f_nop);
			di += ds;
		}
		/* line 16, already in place */
		di += ds;
	}
}

/* X x Y -> X x Y*17/16 */
void upscale_clut_nn_y_16_17(u8 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height)
{
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 8; j++) {
			h_copy(di, ds, si, ss, width, f_nop);
		}
		di +=  ds;
		for (j = 0; j < 8; j++) {
			h_copy(di, ds, si, ss, width, f_nop);
		}

		di -= 9*ds;
		v_copy(&di[0], &di[-ds], width, f_nop);
		di += 9*ds;
	}
}

void upscale_rgb_nn_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 8; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		di +=  ds;
		for (j = 0; j < 8; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}

		di -= 9*ds;
		v_copy(&di[0], &di[-ds], width, f_nop);
		di += 9*ds;
	}
}

void upscale_rgb_snn_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 8; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		di +=  ds;
		for (j = 0; j < 8; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}

		/* mix lines 6-8 */
		di -= 9*ds;
		v_mix(&di[0], &di[-ds], &di[ds], width, p_05, f_nop);
		v_mix(&di[-ds], &di[-2*ds], &di[-ds], width, p_05, f_nop);
		v_mix(&di[ ds], &di[ ds], &di[ 2*ds], width, p_05, f_nop);
		di += 9*ds;
	}
}

void upscale_rgb_bl2_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 4; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		di +=  ds;
		for (j = 0; j < 12; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		/* mix lines 4-11 */
		di -= 13*ds;
			v_mix(&di[0], &di[-ds], &di[ds], width, p_05, f_nop);
		for (j = 0; j < 7; j++) {
			di += ds;
			v_mix(&di[0], &di[0], &di[ds], width, p_05, f_nop);
		}
		di += 6*ds;
	}
}

void upscale_rgb_bl4_y_16_17(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j;

	for (y = 0; y < height; y += 16) {
		for (j = 0; j < 2; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		di += ds;
		for (j = 0; j < 14; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		di -= 15*ds;
		/* mixing line 2: line 1 = -ds, line 2 = +ds */
			v_mix(&di[0], &di[-ds], &di[ds], width, p_025, f_nop);
			di += ds;
		/* mixing lines 3-5: line n-1 = 0, line n = +ds */
		for (j = 0; j < 3; j++) {
			v_mix(&di[0], &di[0], &di[ds], width, p_025, f_nop);
			di += ds;
			}
		/* mixing lines 6-9 */
		for (j = 0; j < 4; j++) {
			v_mix(&di[0], &di[0], &di[ds], width, p_05, f_nop);
			di += ds;
		}
		/* mixing lines 10-13 */
		for (j = 0; j < 4; j++) {
			v_mix(&di[0], &di[0], &di[ds], width, p_075, f_nop);
			di += ds;
		}
		/* lines 14-16, already in place */
		di += 3*ds;
	}
}

/* X x Y -> X*2/1 x Y, e.g. for X 160->320 (GG) */
void upscale_clut_nn_x_1_2(u8 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_nn_1_2(di, ds, si, ss, width, f_nop);
	}
}

void upscale_rgb_nn_x_1_2(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_nn_1_2(di, ds, si, ss, width, f_pal);
	}
}

void upscale_rgb_bl2_x_1_2(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y;

	for (y = 0; y < height; y++) {
		h_upscale_bl2_1_2(di, ds, si, ss, width, f_pal);
	}
}

/* X x Y -> X*2/1 x Y*5/3 (GG) */
void upscale_clut_nn_x_1_2_y_3_5(u8 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height)
{
	int swidth = width * 2;
	int y, j;

	for (y = 0; y < height; y += 3) {
		/* lines 0,2,4 */
		for (j = 0; j < 3; j++) {
			h_upscale_nn_1_2(di, ds, si, ss, width, f_nop);
			di += ds;
		}
		/* lines 1,3 */
		di -= 5*ds;
		for (j = 0; j < 2; j++) {
			v_copy(&di[0], &di[-ds], swidth, f_nop);
			di += 2*ds;
		}
	}
}

void upscale_rgb_nn_x_1_2_y_3_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 2;
	int y, j;

	for (y = 0; y < height; y += 3) {
		for (j = 0; j < 3; j++) {
			h_upscale_nn_1_2(di, ds, si, ss, width, f_pal);
			di += ds;
		}
		di -= 5*ds;
		for (j = 0; j < 2; j++) {
			v_copy(&di[0], &di[-ds], swidth, f_nop);
			di += 2*ds;
		}
	}
}

void upscale_rgb_bl2_x_1_2_y_3_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 2;
	int y, j;

	for (y = 0; y < height; y += 3) {
		for (j = 0; j < 3; j++) {
			h_upscale_bl2_1_2(di, ds, si, ss, width, f_pal);
			di += ds;
		}
		di -= 5*ds;
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[-ds], &di[ds], swidth, p_05, f_nop);
			di += 2*ds;
		}
	}
}

void upscale_rgb_bl4_x_1_2_y_3_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int swidth = width * 2;
	int y, j, d;

	/* for 1st block backwards reference virtually duplicate source line 0 */
	for (y = 0, d = 2*ds; y < height; y += 3, d = -ds) {
		di += 2*ds;
		for (j = 0; j < 3; j++) {
			h_upscale_bl2_1_2(di, ds, si, ss, width, f_pal);
		}
		di -= 5*ds;
		v_mix(&di[0], &di[d ], &di[2*ds], swidth, p_05, f_nop); /*-1+0 */
		di += ds;
		v_mix(&di[0], &di[ds], &di[2*ds], swidth, p_075, f_nop);/* 0+1 */
		di += ds;
		v_mix(&di[0], &di[ 0], &di[  ds], swidth, p_025, f_nop);/* 0+1 */
		di += ds;
		v_mix(&di[0], &di[ 0], &di[  ds], swidth, p_05, f_nop); /* 1+2 */
		di += 2*ds;
	}
}

/* X x Y -> X x Y*5/3, e.g. for Y 144->240 (GG) */
void upscale_clut_nn_y_3_5(u8 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height)
{
	int y, j;

	for (y = 0; y < height; y += 3) {
		/* lines 0,2,4 */
		for (j = 0; j < 3; j++) {
			h_copy(di, ds, si, ss, width, f_nop);
			di += ds;
		}
		/* lines 1,3 */
		di -= 5*ds;
		for (j = 0; j < 2; j++) {
			v_copy(&di[0], &di[-ds], width, f_nop);
			di += 2*ds;
		}
	}
}

void upscale_rgb_nn_y_3_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j;

	for (y = 0; y < height; y += 3) {
		for (j = 0; j < 3; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
			di += ds;
		}
		di -= 5*ds;
		for (j = 0; j < 2; j++) {
			v_copy(&di[0], &di[-ds], width, f_nop);
			di += 2*ds;
		}
	}
}

void upscale_rgb_bl2_y_3_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j;

	for (y = 0; y < height; y += 3) {
		for (j = 0; j < 3; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
			di += ds;
		}
		di -= 5*ds;
		for (j = 0; j < 2; j++) {
			v_mix(&di[0], &di[-ds], &di[ds], width, p_05, f_nop);
			di += 2*ds;
		}
	}
}

void upscale_rgb_bl4_y_3_5(u16 *__restrict di, int ds, u8 *__restrict si, int ss, int width, int height, u16 *pal)
{
	int y, j, d;

	/* for 1st block backwards reference virtually duplicate source line 0 */
	for (y = 0, d = 2*ds; y < height; y += 3, d = -ds) {
		di += 2*ds;
		for (j = 0; j < 3; j++) {
			h_copy(di, ds, si, ss, width, f_pal);
		}
		di -= 5*ds;
		v_mix(&di[0], &di[d ], &di[2*ds], width, p_05, f_nop); /*-1+0 */
		di += ds;
		v_mix(&di[0], &di[ds], &di[2*ds], width, p_075, f_nop);/* 0+1 */
		di += ds;
		v_mix(&di[0], &di[ 0], &di[  ds], width, p_025, f_nop);/* 0+1 */
		di += ds;
		v_mix(&di[0], &di[ 0], &di[  ds], width, p_05, f_nop); /* 1+2 */
		di += 2*ds;
	}
}

/* Quantized blend weight toward the second source pixel, in 1/4 units (0..4).
 * filter: 1 = snn, 2 = bl2, 3 = bl4 (see the scaler types at the top). */
static inline int blend_qweight(u32 frac, int filter)
{
	if (filter == 1)	/* snn: average only if neither pixel covers >65% */
		return (frac >= 22938 && frac <= 42598) ? 2 : 0;
	if (filter == 2)	/* bl2: 2-level, round to 1/2 (0, 2, 4) */
		return ((frac + 16384) >> 15) * 2;
	return (frac + 8192) >> 14;	/* bl4: 4-level, round to 1/4 (0..4) */
}

/* Per-filter horizontal blend "lambdas".  Each one handles exactly the q
 * values the corresponding filter can produce, so the generic five-way switch
 * is gone from the inner loop.  channel-safe for RGB565/BGR555. */
#define STRETCH_BLEND_SNN(dst, a, b, q) \
	do { \
		if (q) { p_05(dst, a, b); } \
		else   { dst = a; } \
	} while (0)

#define STRETCH_BLEND_BL2(dst, a, b, q) \
	do { \
		if (q == 0)      { dst = a; } \
		else if (q == 4) { dst = b; } \
		else             { p_05(dst, a, b); } \
	} while (0)

#define STRETCH_BLEND_BL4(dst, a, b, q) \
	do { \
		u16 t; \
		if (q == 0)      { dst = a; } \
		else if (q == 4) { dst = b; } \
		else if (q == 2) { p_05(dst, a, b); } \
		else if (q == 1) { p_075(dst, a, b); } \
		else             { p_025(dst, a, b); } \
	} while (0)

/* Blend two source rows with weight q/4 toward b. */
static void blend_row(u16 *d, const u16 *a, const u16 *b, int n, int q)
{
	u16 t;
	int i;

	switch (q) {
	case 0:
		if (d != a)
			memcpy(d, a, n * sizeof(*d));
		break;
	case 1:
		/* p_075/p_025 expand to two statements, so keep the braces */
		for (i = 0; i < n; i++) {
			p_075(d[i], a[i], b[i]);
		}
		break;
	case 2:
		for (i = 0; i < n; i++)
			p_05(d[i], a[i], b[i]);
		break;
	case 3:
		for (i = 0; i < n; i++) {
			p_025(d[i], a[i], b[i]);
		}
		break;
	default:
		if (d != b)
			memcpy(d, b, n * sizeof(*d));
		break;
	}
}

/* Generate the horizontal pass for one filter.  "blend" is the per-pixel
 * lambda; "clamped" selects the right-edge handling at compile time.  Keeping
 * the loop in a macro gives each filter its own copy with no runtime
 * filter/switch. */
#define STRETCH_H_ROW(name, clamped, blend) \
static void name(u16 *d, const u16 *s, int sw, int dw, \
		 const u16 *xmap, const u8 *qmap) \
{ \
	int i; \
	for (i = 0; i < dw; i++) { \
		u32 sx = xmap[i]; \
		u16 a = s[sx]; \
		u16 b = s[sx + (clamped ? (sx + 1 < (u32)sw) : 1)]; \
		blend(d[i], a, b, qmap[i]); \
	} \
}

STRETCH_H_ROW(blend_row_h_snn, 0, STRETCH_BLEND_SNN)
STRETCH_H_ROW(blend_row_h_snn_clamped, 1, STRETCH_BLEND_SNN)
STRETCH_H_ROW(blend_row_h_bl2, 0, STRETCH_BLEND_BL2)
STRETCH_H_ROW(blend_row_h_bl2_clamped, 1, STRETCH_BLEND_BL2)
STRETCH_H_ROW(blend_row_h_bl4, 0, STRETCH_BLEND_BL4)
STRETCH_H_ROW(blend_row_h_bl4_clamped, 1, STRETCH_BLEND_BL4)

#define STRETCH_H_CALL(f, d, s, sw, dw, xmap, qmap) \
	do { \
		switch (f) { \
		case 1: blend_row_h_snn(d, s, sw, dw, xmap, qmap); break; \
		case 2: blend_row_h_bl2(d, s, sw, dw, xmap, qmap); break; \
		default: blend_row_h_bl4(d, s, sw, dw, xmap, qmap); break; \
		} \
	} while (0)

#define STRETCH_H_CALL_CLAMPED(f, d, s, sw, dw, xmap, qmap) \
	do { \
		switch (f) { \
		case 1: blend_row_h_snn_clamped(d, s, sw, dw, xmap, qmap); break; \
		case 2: blend_row_h_bl2_clamped(d, s, sw, dw, xmap, qmap); break; \
		default: blend_row_h_bl4_clamped(d, s, sw, dw, xmap, qmap); break; \
		} \
	} while (0)

/* Scratch space for the separable path. It is reused between frames and only
 * grows when the source or destination size changes. */
static u16 *stretch_row;
static u16 *stretch_xmap;
static u8 *stretch_qmap;
static int stretch_row_len, stretch_map_len;

static int stretch_prepare_row(int sw)
{
	u16 *p;

	if (stretch_row_len >= sw + 1)
		return 1;
	p = realloc(stretch_row, (sw + 1) * sizeof(*p));
	if (p == NULL)
		return 0;
	stretch_row = p;
	stretch_row_len = sw + 1;
	return 1;
}

static int stretch_prepare_map(int dw)
{
	u16 *xp;
	u8 *qp;

	if (stretch_map_len >= dw)
		return 1;
	xp = realloc(stretch_xmap, dw * sizeof(*xp));
	if (xp == NULL)
		return 0;
	stretch_xmap = xp;
	qp = realloc(stretch_qmap, dw * sizeof(*qp));
	if (qp == NULL)
		return 0;
	stretch_qmap = qp;
	stretch_map_len = dw;
	return 1;
}

static void stretch_map_fill(int dw, u32 xstep, int filter)
{
	u32 xpos = 0;
	int i;

	for (i = 0; i < dw; i++, xpos += xstep) {
		stretch_xmap[i] = xpos >> 16;
		stretch_qmap[i] = blend_qweight(xpos & 0xffff, filter);
	}
}

/* Arbitrary-ratio 16bpp scaler, source already in destination pixel format.
 * The filter is separable: blend rows vertically at source width, then scale
 * the result horizontally. Only-vertical and only-horizontal cases skip the
 * unused pass. */
void upscale_rgb_stretch(u16 *__restrict di, int ds, const u16 *__restrict si, int ss,
                         int dw, int dh, int sw, int sh, int filter)
{
	u32 xstep, ystep, xpos, ypos;
	int i, j;

	if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
		return;

	/* nothing to scale: plain row copy */
	if (dw == sw && dh == sh) {
		for (j = 0; j < dh; j++) {
			memcpy(di, si, dw * sizeof(u16));
			di += ds, si += ss;
		}
		return;
	}

	xstep = (u32)sw * 65536 / dw;
	ystep = (u32)sh * 65536 / dh;

	if (filter <= 0) {
		/* exact integer ratio: replicate columns and rows */
		if (dw % sw == 0 && dh % sh == 0) {
			int fx = dw / sw, fy = dh / sh;

			for (j = 0; j < sh; j++) {
				const u16 *srow = si + j * ss;
				u16 *row = di + j * fy * ds;
				int k;

				if (fx == 1) {
					memcpy(row, srow, dw * sizeof(u16));
				} else {
					for (k = 0; k < sw; k++) {
						u16 p = srow[k];
						u16 *dp = row + k * fx;

						for (i = 0; i < fx; i++)
							dp[i] = p;
					}
				}
				for (i = 1; i < fy; i++)
					memcpy(row + i * ds, row, dw * sizeof(u16));
			}
			return;
		}

		/* no horizontal scaling: copy whole rows */
		if (dw == sw) {
			for (j = 0, ypos = 0; j < dh; j++, ypos += ystep) {
				memcpy(di, si + (ypos >> 16) * ss, dw * sizeof(u16));
				di += ds;
			}
			return;
		}

		/* Q16 stepping, unrolled so the index math is independent */
		for (j = 0, ypos = 0; j < dh; j++, ypos += ystep) {
			const u16 *srow = si + (ypos >> 16) * ss;

			xpos = 0;
			for (i = 0; i + 8 <= dw; i += 8) {
				di[i+0] = srow[(xpos             ) >> 16];
				di[i+1] = srow[(xpos +   xstep    ) >> 16];
				di[i+2] = srow[(xpos + 2*xstep    ) >> 16];
				di[i+3] = srow[(xpos + 3*xstep    ) >> 16];
				di[i+4] = srow[(xpos + 4*xstep    ) >> 16];
				di[i+5] = srow[(xpos + 5*xstep    ) >> 16];
				di[i+6] = srow[(xpos + 6*xstep    ) >> 16];
				di[i+7] = srow[(xpos + 7*xstep    ) >> 16];
				xpos += 8*xstep;
			}
			for (; i < dw; i++, xpos += xstep)
				di[i] = srow[xpos >> 16];
			di += ds;
		}
		return;
	}

	/* vertical scaling only */
	if (dw == sw) {
		for (j = 0, ypos = 0; j < dh; j++, ypos += ystep) {
			u32 sy = ypos >> 16;
			int qy = blend_qweight(ypos & 0xffff, filter);
			const u16 *r0 = si + sy * ss;
			const u16 *r1 = sy + 1 < (u32)sh ? r0 + ss : r0;

			blend_row(di, r0, r1, dw, qy);
			di += ds;
		}
		return;
	}

	if (sh == dh) {
		/* horizontal scaling only */
		if (!stretch_prepare_map(dw)) {
			/* scratch allocation failed: nearest-neighbour still works */
			upscale_rgb_stretch(di, ds, si, ss, dw, dh, sw, sh, 0);
			return;
		}
		stretch_map_fill(dw, xstep, filter);
		for (j = 0; j < dh; j++) {
			STRETCH_H_CALL_CLAMPED(filter, di, si + j * ss, sw, dw,
					       stretch_xmap, stretch_qmap);
			di += ds;
		}
		return;
	}

	if (!stretch_prepare_row(sw) || !stretch_prepare_map(dw)) {
		/* scratch allocation failed: nearest-neighbour still works */
		upscale_rgb_stretch(di, ds, si, ss, dw, dh, sw, sh, 0);
		return;
	}
	stretch_map_fill(dw, xstep, filter);

	for (j = 0, ypos = 0; j < dh; j++, ypos += ystep) {
		u32 sy = ypos >> 16;
		int qy = blend_qweight(ypos & 0xffff, filter);
		const u16 *r0 = si + sy * ss;
		const u16 *r1 = sy + 1 < (u32)sh ? r0 + ss : r0;

		blend_row(stretch_row, r0, r1, sw, qy);
		stretch_row[sw] = stretch_row[sw - 1];
		STRETCH_H_CALL(filter, di, stretch_row, sw, dw,
			       stretch_xmap, stretch_qmap);
		di += ds;
	}
}

