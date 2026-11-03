/*
 * PicoDrive
 * (C) notaz, 2013
 * (C) irixxxx, 2020-2024
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <string.h>

#include "../libpicofe/input.h"
#include "../libpicofe/plat.h"
#include "../libpicofe/plat_sdl.h"
#include "../libpicofe/in_sdl.h"
#include "../libpicofe/gl.h"
#include "emu.h"
#include "menu_pico.h"
#include "input_pico.h"
#include "plat_sdl.h"
#include "version.h"

#include <pico/pico_int.h>

static void *shadow_fb;
static int shadow_size;
static struct area { int w, h; } area;

static int resized;

static struct in_pdata in_sdl_platform_data;

static int hide_cursor;

static int sound_rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 53000, -1 };
struct plat_target plat_target = { .sound_rates = sound_rates };

#if defined __MIYOO__
const char *plat_device = "miyoo";
#elif defined __GCW0__
const char *plat_device = "gcw0";
#elif defined __RETROFW__
const char *plat_device = "retrofw";
#elif defined __DINGUX__
const char *plat_device = "dingux";
#else
const char *plat_device = "";
#endif

int plat_parse_arg(int argc, char *argv[], int *x)
{
#if defined __OPENDINGUX__
	if (*plat_device == '\0' && strcasecmp(argv[*x], "-device") == 0) {
		plat_device = argv[++(*x)];
		return 0;
	}
#endif
	return 1;
}

void plat_early_init(void)
{
}

int plat_target_init(void)
{
#if defined __ODBETA__
	if (*plat_device == '\0') {
		/* ODbeta should always have a device tree, get the model info from there */
		FILE *f = fopen("/proc/device-tree/compatible", "r");
		if (f) {
			char buf[10];
			int c = fread(buf, 1, sizeof(buf)-1, f);
			buf[c] = '\0';
			if (strncmp(buf, "gcw,", 4) == 0)
				plat_device = "gcw0";
		}
	}
#endif
	return 0;
}

void plat_target_finish(void)
{
}

/* YUV stuff */
static int yuv_ry[32], yuv_gy[32], yuv_by[32];
static unsigned char yuv_u[32 * 2], yuv_v[32 * 2];
static unsigned char yuv_y[256];
static struct uyvy { uint32_t y:8; uint32_t vyu:24; } yuv_uyvy[65536];

void bgr_to_uyvy_init(void)
{
	int i, v;

	/* init yuv converter:
	    y0 = (int)((0.299f * r0) + (0.587f * g0) + (0.114f * b0));
	    y1 = (int)((0.299f * r1) + (0.587f * g1) + (0.114f * b1));
	    u = (int)(8 * 0.565f * (b0 - y0)) + 128;
	    v = (int)(8 * 0.713f * (r0 - y0)) + 128;
	*/
	for (i = 0; i < 32; i++) {
		yuv_ry[i] = (int)(0.299f * i * 65536.0f + 0.5f);
		yuv_gy[i] = (int)(0.587f * i * 65536.0f + 0.5f);
		yuv_by[i] = (int)(0.114f * i * 65536.0f + 0.5f);
	}
	for (i = -32; i < 32; i++) {
		v = (int)(8 * 0.565f * i) + 128;
		if (v < 0)
			v = 0;
		if (v > 255)
			v = 255;
		yuv_u[i + 32] = v;
		v = (int)(8 * 0.713f * i) + 128;
		if (v < 0)
			v = 0;
		if (v > 255)
			v = 255;
		yuv_v[i + 32] = v;
	}
	// valid Y range seems to be 16..235
	for (i = 0; i < 256; i++) {
		yuv_y[i] = 16 + 219 * i / 32;
	}
	// everything combined into one large array for speed
	for (i = 0; i < 65536; i++) {
		int r = (i >> 11) & 0x1f, g = (i >> 6) & 0x1f, b = (i >> 0) & 0x1f;
		int y = (yuv_ry[r] + yuv_gy[g] + yuv_by[b]) >> 16;
		yuv_uyvy[i].y = yuv_y[y];
#if CPU_IS_LE
		yuv_uyvy[i].vyu = (yuv_v[r-y + 32] << 16) | (yuv_y[y] << 8) | yuv_u[b-y + 32];
#else
		yuv_uyvy[i].vyu = (yuv_u[b-y + 32] << 16) | (yuv_y[y] << 8) | yuv_v[r-y + 32];
#endif
	}
}

void rgb565_to_uyvy(void *d, const void *s, int w, int h, int pitch, int dpitch, int x2)
{
	uint32_t *dst = d;
	const uint16_t *src = s;
	int i;

	if (x2) while (h--) {
		for (i = w; i >= 4; src += 4, dst += 4, i -= 4)
		{
			struct uyvy *uyvy0 = yuv_uyvy + src[0], *uyvy1 = yuv_uyvy + src[1];
			struct uyvy *uyvy2 = yuv_uyvy + src[2], *uyvy3 = yuv_uyvy + src[3];
#if CPU_IS_LE
			dst[0] = (uyvy0->y << 24) | uyvy0->vyu;
			dst[1] = (uyvy1->y << 24) | uyvy1->vyu;
			dst[2] = (uyvy2->y << 24) | uyvy2->vyu;
			dst[3] = (uyvy3->y << 24) | uyvy3->vyu;
#else
			dst[0] = uyvy0->y | (uyvy0->vyu << 8);
			dst[1] = uyvy1->y | (uyvy1->vyu << 8);
			dst[2] = uyvy2->y | (uyvy2->vyu << 8);
			dst[3] = uyvy3->y | (uyvy3->vyu << 8);
#endif
		}
		src += pitch - (w-i);
		dst += (dpitch - 2*(w-i))/2;
	} else while (h--) {
		for (i = w; i >= 4; src += 4, dst += 2, i -= 4)
		{
			struct uyvy *uyvy0 = yuv_uyvy + src[0], *uyvy1 = yuv_uyvy + src[1];
			struct uyvy *uyvy2 = yuv_uyvy + src[2], *uyvy3 = yuv_uyvy + src[3];
#if CPU_IS_LE
			dst[0] = (uyvy1->y << 24) | uyvy0->vyu;
			dst[1] = (uyvy3->y << 24) | uyvy2->vyu;
#else
			dst[0] = uyvy1->y | (uyvy0->vyu << 8);
			dst[1] = uyvy3->y | (uyvy2->vyu << 8);
#endif
		}
		src += pitch - (w-i);
		dst += (dpitch - (w-i))/2;
	}
}

/* copy a 16bpp image into a rectangle, stretching as needed */
static void copy_stretch(void *dst, int dpp, const void *src,
                         int sw, int sh, int spp, int x, int y, int w, int h)
{
	uint16_t *__restrict d = (uint16_t *)dst + y*dpp + x;
	const uint16_t *__restrict s = src;
	uint32_t xstep, ystep, ypos;
	int i, j;

	if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
		return;

	// Q16 source steps; avoids a division for every output pixel.
	xstep = (uint32_t)sw * 65536 / w;
	ystep = (uint32_t)sh * 65536 / h;
	ypos = 0;

	// no horizontal scaling: copy whole rows
	if (sw == w) {
		for (j = 0; j < h; j++) {
			const uint16_t *srow = s + (ypos >> 16) * spp;
			memcpy(d, srow, w * sizeof(uint16_t));
			d += dpp;
			ypos += ystep;
		}
		return;
	}

	for (j = 0; j < h; j++) {
		const uint16_t *srow = s + (ypos >> 16) * spp;
		uint32_t xpos = 0;

		// unrolled so the source index calculations are independent
		for (i = 0; i + 8 <= w; i += 8) {
			d[i+0] = srow[(xpos             ) >> 16];
			d[i+1] = srow[(xpos +   xstep    ) >> 16];
			d[i+2] = srow[(xpos + 2*xstep    ) >> 16];
			d[i+3] = srow[(xpos + 3*xstep    ) >> 16];
			d[i+4] = srow[(xpos + 4*xstep    ) >> 16];
			d[i+5] = srow[(xpos + 5*xstep    ) >> 16];
			d[i+6] = srow[(xpos + 6*xstep    ) >> 16];
			d[i+7] = srow[(xpos + 7*xstep    ) >> 16];
			xpos += 8*xstep;
		}
		for (; i < w; i++) {
			d[i] = srow[xpos >> 16];
			xpos += xstep;
		}
		d += dpp;
		ypos += ystep;
	}
}

static inline void get_aspect_rect(SDL_Rect *r, int win_w, int win_h)
{
	int x, y, w, h;

	plat_aspect_rect(&x, &y, &w, &h, win_w, win_h);
	r->x = x;
	r->y = y;
	r->w = w;
	r->h = h;
}

static int rect_needs_bars(const SDL_Rect *r, int win_w, int win_h)
{
	return r->x != 0 || r->y != 0 || r->w != win_w || r->h != win_h;
}

static int clear_buf_cnt, clear_stat_cnt;

static void resize_buffers(void)
{
	// update pitch as it is needed for resizing, and the menu bg scaler
	if (plat_sdl_overlay || plat_sdl_gl_active)
		g_menuscreen_pp = g_menuscreen_w;
	else
		g_menuscreen_pp = plat_sdl_screen->pitch / 2;

	// make sure the shadow buffers are big enough in case of resize
	if (shadow_size < g_menuscreen_pp * g_menuscreen_h * 2) {
		shadow_size = g_menuscreen_pp * g_menuscreen_h * 2;
		shadow_fb = realloc(shadow_fb, shadow_size);
		g_menubg_ptr = realloc(g_menubg_ptr, shadow_size);
	}
}

void plat_video_set_size(int w, int h)
{
	if (area.w != w || area.h != h) {
		area = (struct area) { w, h };

		if (plat_sdl_overlay || plat_sdl_gl_active || !plat_sdl_is_windowed()) {
			// create surface for overlays, or try using a hw scaler
			if (plat_sdl_change_video_mode(w, h, 0) < 0) {
				// failed, revert to original resolution
				area = (struct area) { g_screen_width,g_screen_height };
				plat_sdl_change_video_mode(g_screen_width, g_screen_height, 0);
			}
			plat_sdl_gl_scaling(currentConfig.filter);
		}
		if (plat_sdl_overlay || plat_sdl_gl_active) {
			// use shadow buffer for overlays
			g_screen_width = area.w;
			g_screen_height = area.h;
			g_screen_ppitch = area.w;
			g_screen_ptr = shadow_fb;
		}
		else if (plat_sdl_is_windowed()) {
			// shadow buffer, scaled to the window
			g_screen_width = 320;
			g_screen_height = 240;
			g_screen_ppitch = 320;
			g_screen_ptr = shadow_fb;
		}
		else {
			// unscaled SDL window buffer can be used directly
			g_screen_width = plat_sdl_screen->w;
			g_screen_height = plat_sdl_screen->h;
			g_screen_ppitch = plat_sdl_screen->pitch/2;
			g_screen_ptr = plat_sdl_screen->pixels;
		}
	}
}

void plat_video_set_shadow(int w, int h)
{
	g_screen_width = w;
	g_screen_height = h;
	g_screen_ppitch = w;
	g_screen_ptr = shadow_fb;
}


static float gl_verts[12];
static int gl_win_w = -1, gl_win_h = -1, gl_aspect = -1;

void plat_video_flip(void)
{
	SDL_Rect r;

	if (plat_sdl_overlay != NULL) {
		get_aspect_rect(&r, plat_sdl_screen->w, plat_sdl_screen->h);

		if (rect_needs_bars(&r, plat_sdl_screen->w, plat_sdl_screen->h))
			SDL_FillRect(plat_sdl_screen, NULL, 0);

		SDL_LockYUVOverlay(plat_sdl_overlay);
		if (area.w <= plat_sdl_overlay->w && area.h <= plat_sdl_overlay->h)
			rgb565_to_uyvy(plat_sdl_overlay->pixels[0], shadow_fb,
					area.w, area.h, g_screen_ppitch,
					plat_sdl_overlay->pitches[0]/2,
					plat_sdl_overlay->w >= 2*area.w);
		SDL_UnlockYUVOverlay(plat_sdl_overlay);
		SDL_DisplayYUVOverlay(plat_sdl_overlay, &r);
	}
	else if (plat_sdl_gl_active) {
		// The vertices only depend on the window size and aspect setting.
		int w = g_screen_ppitch, h = g_screen_height;
		int win_w = plat_sdl_screen->w, win_h = plat_sdl_screen->h;

		if (win_w != gl_win_w || win_h != gl_win_h ||
		    currentConfig.aspect != gl_aspect) {
			get_aspect_rect(&r, win_w, win_h);

			// normalized device coords
			float x0 = 2.0f * r.x / win_w - 1.0f;
			float x1 = 2.0f * (r.x + r.w) / win_w - 1.0f;
			float y1 = 1.0f - 2.0f * r.y / win_h;
			float y0 = 1.0f - 2.0f * (r.y + r.h) / win_h;

			gl_verts[0] = x0; gl_verts[1] = y1; gl_verts[2] = 0.0f;
			gl_verts[3] = x1; gl_verts[4] = y1; gl_verts[5] = 0.0f;
			gl_verts[6] = x0; gl_verts[7] = y0; gl_verts[8] = 0.0f;
			gl_verts[9] = x1; gl_verts[10] = y0; gl_verts[11] = 0.0f;

			gl_win_w = win_w;
			gl_win_h = win_h;
			gl_aspect = currentConfig.aspect;
		}

		gl_flip_v(shadow_fb, w, h, gl_verts);
	}
	else {
		int copy = g_screen_ptr != plat_sdl_screen->pixels;
		if (copy) {
			get_aspect_rect(&r, plat_sdl_screen->w, plat_sdl_screen->h);
			if (rect_needs_bars(&r, plat_sdl_screen->w, plat_sdl_screen->h))
				memset(plat_sdl_screen->pixels, 0, plat_sdl_screen->pitch*plat_sdl_screen->h);
			copy_stretch(plat_sdl_screen->pixels,
				     plat_sdl_screen->pitch/2,
				     shadow_fb, g_screen_width, g_screen_height,
				     g_screen_ppitch, r.x, r.y, r.w, r.h);
		}

		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_UnlockSurface(plat_sdl_screen);
		SDL_Flip(plat_sdl_screen);

		if (!copy) {
			g_screen_ppitch = plat_sdl_screen->pitch/2;
			g_screen_ptr = plat_sdl_screen->pixels;
			plat_video_set_buffer(g_screen_ptr);
		}

		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_LockSurface(plat_sdl_screen);

		if (clear_buf_cnt) {
			memset(plat_sdl_screen->pixels, 0, plat_sdl_screen->pitch*plat_sdl_screen->h);
			clear_buf_cnt--;
		}
	}

	// take over resized settings for the physical SDL surface
	if (resized && plat_sdl_is_windowed() &&
	    SDL_WM_GrabInput(SDL_GRAB_ON) == SDL_GRAB_ON) {
		plat_sdl_change_video_mode(g_menuscreen_w, g_menuscreen_h, 1);
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		resize_buffers();

		// force upper layer to use new dimensions
		plat_video_set_shadow(g_screen_width, g_screen_height);
		plat_video_set_buffer(g_screen_ptr);
		rendstatus_old = -1;
		resized = 0;
	}

	if (clear_stat_cnt) {
		unsigned short *d = (unsigned short *)g_screen_ptr + g_screen_ppitch * g_screen_height;
		int l = g_screen_ppitch * 8;
		memset((int *)(d - l), 0, l * 2);
		clear_stat_cnt--;
	}
}

void plat_video_wait_vsync(void)
{
}

void plat_video_clear_status(void)
{
	clear_stat_cnt = 3; // do it thrice in case of triple buffering
}

void plat_video_clear_buffers(void)
{
	int count = g_menuscreen_w * g_menuscreen_h;
	if (count < area.w * area.h) count = area.w * area.h;
	memset(shadow_fb, 0, count * 2);
	if (plat_sdl_overlay)
		plat_sdl_overlay_clear();
	memset(plat_sdl_screen->pixels, 0, plat_sdl_screen->pitch*plat_sdl_screen->h);
	clear_buf_cnt = 3; // do it thrice in case of triple buffering
}

// keep the window size in sync with the configured aspect ratio, but
// only if it currently uses one of the two preset sizes
static void plat_sdl_sync_window_aspect(void)
{
	static int last_aspect = -1;
	int scale, want_w, want_h;

	if (last_aspect == currentConfig.aspect)
		return;
	last_aspect = currentConfig.aspect;

	if (!plat_sdl_is_windowed() || plat_target.vout_fullscreen != 0)
		return;

	scale = g_menuscreen_h / 240;
	want_h = plat_window_height(scale);
	want_w = plat_window_width(scale, currentConfig.aspect);

	if (g_menuscreen_h == want_h &&
	    (g_menuscreen_w == plat_window_width(scale, EOPT_ASPECT_43) ||
	     g_menuscreen_w == plat_window_width(scale, EOPT_ASPECT_16_9)) &&
	    g_menuscreen_w != want_w)
		plat_set_window(want_w, want_h);
}

void plat_video_menu_update(void)
{
	plat_sdl_sync_window_aspect();

	// WM may grab input while resizing the window; our own window resizing
	// is only safe if the WM isn't active anymore, so try to grab input.
	if (plat_sdl_is_windowed() && SDL_WM_GrabInput(SDL_GRAB_ON) == SDL_GRAB_ON) {
		plat_sdl_change_video_mode(g_menuscreen_w, g_menuscreen_h, 1);
		// w/h might change in change_video_mode, e.g. for fullscreen
		g_menuscreen_w = plat_sdl_screen->w;
		g_menuscreen_h = plat_sdl_screen->h;
		plat_sdl_gl_scaling(currentConfig.filter);
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		resize_buffers();
	}
}

void plat_video_menu_enter(int is_rom_loaded)
{
	plat_sdl_sync_window_aspect();

	if (SDL_MUSTLOCK(plat_sdl_screen))
		SDL_UnlockSurface(plat_sdl_screen);
	plat_sdl_change_video_mode(g_menuscreen_w, g_menuscreen_h, 1);
}

void plat_video_menu_begin(void)
{
	plat_video_menu_update(); // just in case

	if (plat_sdl_overlay || plat_sdl_gl_active)
		g_menuscreen_ptr = shadow_fb;
	else {
		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_LockSurface(plat_sdl_screen);
		g_menuscreen_ptr = plat_sdl_screen->pixels;
	}
}

void plat_video_menu_end(void)
{
	if (plat_sdl_overlay != NULL) {
		SDL_Rect dstrect =
			{ 0, 0, plat_sdl_screen->w, plat_sdl_screen->h };

		SDL_LockYUVOverlay(plat_sdl_overlay);
		if (g_menuscreen_w <= plat_sdl_overlay->w && g_menuscreen_h <= plat_sdl_overlay->h)
			rgb565_to_uyvy(plat_sdl_overlay->pixels[0], shadow_fb,
				g_menuscreen_w, g_menuscreen_h, g_menuscreen_pp,
				plat_sdl_overlay->pitches[0]/2,
				plat_sdl_overlay->w >= 2 * g_menuscreen_w);
		SDL_UnlockYUVOverlay(plat_sdl_overlay);

		SDL_DisplayYUVOverlay(plat_sdl_overlay, &dstrect);
	}
	else if (plat_sdl_gl_active) {
		gl_flip(g_menuscreen_ptr, g_menuscreen_pp, g_menuscreen_h);
	}
	else {
		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_UnlockSurface(plat_sdl_screen);
		SDL_Flip(plat_sdl_screen);
	}
	g_menuscreen_ptr = NULL;
}

void plat_video_menu_leave(void)
{
}

void plat_video_loop_prepare(void)
{
	int w = g_menuscreen_w, h = g_menuscreen_h;

	// take over any new vout settings
	area = (struct area) { 0, 0 };
//	plat_sdl_change_video_mode(0, 0, 0);

	// switch over to scaled output if available, but keep the aspect ratio
	if (plat_sdl_overlay || plat_sdl_gl_active)
		w = 320, h = 240;

	g_screen_width = w, g_screen_height = h;
	plat_video_set_size(w, h);

	if (!(plat_sdl_overlay || plat_sdl_gl_active))
		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_LockSurface(plat_sdl_screen);

	plat_video_set_buffer(g_screen_ptr);
}

void plat_show_cursor(int on)
{
	SDL_ShowCursor(on && !hide_cursor);
}

int plat_grab_cursor(int on)
{
	SDL_WM_GrabInput(on ? SDL_GRAB_ON : SDL_GRAB_OFF);
	return on;
}

int plat_has_wm(void)
{
	return plat_sdl_is_windowed();
}

void plat_set_window(int w, int h)
{
	g_menuscreen_w = w;
	g_menuscreen_h = h;
	resized = 1;
}

// aspect-correct image rect centered in a win_w x win_h area
void plat_aspect_rect(int *x, int *y, int *w, int *h, int win_w, int win_h)
{
	int an = currentConfig.aspect == EOPT_ASPECT_16_9 ? 16 : 4;
	int ad = currentConfig.aspect == EOPT_ASPECT_16_9 ? 9 : 3;
	int dw = win_w, dh = dw * ad / an;

	if (dh > win_h) {
		dh = win_h;
		dw = dh * an / ad;
	}
	*x = (win_w - dw) / 2;
	*y = (win_h - dh) / 2;
	*w = dw;
	*h = dh;
}

static void plat_sdl_resize(int w, int h)
{
	// take over new settings
#if defined(__OPENDINGUX__)
	if (currentConfig.vscaling != EOPT_SCALE_HW && w == 320 && h == 480)
		h = 240;
#endif
	if (g_menuscreen_w != (w & ~3) || g_menuscreen_h != (h & ~3)){
		g_menuscreen_h = h & ~3;
		g_menuscreen_w = w & ~3;
		resized = 1;
	}
}

static void plat_sdl_quit(void)
{
	// for now..
	exit(1);
}

static void plat_sdl_handler(void *event_)
{
  SDL_Event *event = event_;

  if (event->type == SDL_VIDEORESIZE)
    // handle resize here to avoid the internal plat_sdl_change_video_mode
    plat_sdl_resize(event->resize.w, event->resize.h);
  else
    plat_sdl_event_handler(event_);
}

void plat_init(void)
{
	int ret;

	ret = plat_sdl_init();
	if (ret != 0)
		exit(1);

#if defined(__OPENDINGUX__)
	// opendingux on JZ47x0 may falsely report a HW overlay, fix to window
	plat_target.vout_method = 0;
#elif !defined(__MIYOO__) && !defined(__RETROFW__) && !defined(__DINGUX__)
	if (! plat_sdl_is_windowed())
#endif
	{
		hide_cursor = 1;
		SDL_ShowCursor(0);
	}

	plat_sdl_quit_cb = plat_sdl_quit;
//	plat_sdl_resize_cb = plat_sdl_resize;

	SDL_WM_SetCaption("PicoDrive " VERSION, NULL);

	g_menuscreen_pp = g_menuscreen_w;
	g_menuscreen_ptr = NULL;

	shadow_size = g_menuscreen_w * g_menuscreen_h * 2;
	if (shadow_size < 320 * 480 * 2)
		shadow_size = 320 * 480 * 2;

	shadow_fb = calloc(1, shadow_size);
	g_menubg_ptr = calloc(1, shadow_size);
	if (shadow_fb == NULL || g_menubg_ptr == NULL) {
		fprintf(stderr, "OOM\n");
		exit(1);
	}

	g_screen_width = 320;
	g_screen_height = 240;
	g_screen_ppitch = 320;
	g_screen_ptr = shadow_fb;

	plat_target_setup_input();
	in_sdl_platform_data.defbinds = in_sdl_defbinds;
	in_sdl_platform_data.kmap_size = in_sdl_key_map_sz;
	in_sdl_platform_data.key_map = in_sdl_key_map;
	in_sdl_platform_data.jmap_size = in_sdl_joy_map_sz;
	in_sdl_platform_data.joy_map = in_sdl_joy_map;
	in_sdl_platform_data.mmap_size = in_sdl_mouse_map_sz;
	in_sdl_platform_data.mouse_map = in_sdl_mouse_map;
	in_sdl_platform_data.key_names = in_sdl_key_names;
	in_sdl_platform_data.kbd_map = in_sdl_kbd_map;
	in_sdl_platform_data.mouse_device = plat_sdl_is_windowed();
	in_sdl_init(&in_sdl_platform_data, plat_sdl_handler);
	in_probe();

	// create an artificial resize event to initialize mouse scaling
	static SDL_Event ev;
	ev.resize.type = SDL_VIDEORESIZE;
	ev.resize.w = g_menuscreen_w;
	ev.resize.h = g_menuscreen_h;
	SDL_PeepEvents(&ev, 1, SDL_ADDEVENT, SDL_ALLEVENTS);

	bgr_to_uyvy_init();
	linux_menu_init();
}

void plat_finish(void)
{
	free(shadow_fb);
	shadow_fb = NULL;
	free(g_menubg_ptr);
	g_menubg_ptr = NULL;
	plat_sdl_finish();
}
