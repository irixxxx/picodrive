// ------------ gfx options menu ------------

#include "../libpicofe/plat_sdl.h"

static const char *men_scaling_opts[] = { "OFF", "software", "hardware", NULL };
static const char *men_filter_opts[] = { "nearest", "smoother", "bilinear 1", "bilinear 2", NULL };
static const char *men_aspect_opts[] = { "4:3", "16:9", NULL };

static const char h_scale[] = "Hardware scaling might not work on some devices";
static const char h_stype[] = "Scaler algorithm for software scaling";
static const char h_aspect[] = "Scaling to 16:9 for anamorphic widescreen games";

// Window presets: integer vertical scale, width follows the aspect.
static int windowsize_height(int scale)
{
	return plat_window_height(scale);
}

static int windowsize_width(int scale)
{
	return plat_window_width(scale, currentConfig.aspect);
}

static const char *mgn_windowsize(int id, int *offs)
{
	int scale = g_menuscreen_h / 240;

	if ((g_menuscreen_w == windowsize_width(scale) && g_menuscreen_h == windowsize_height(scale)) ||
	    plat_sdl_is_fullscreen())
		sprintf(static_buff, "%dx%d", g_menuscreen_w, g_menuscreen_h);
	else
		sprintf(static_buff, "custom");
	return static_buff;
}

static int mh_windowsize(int id, int keys)
{
	if (keys & (PBTN_LEFT|PBTN_RIGHT)) {
		if (!plat_target.vout_fullscreen) {
			int scale = g_menuscreen_h / 240;
			if (keys & PBTN_RIGHT) scale++;
			if (keys & PBTN_LEFT ) scale--;
			if (scale <= 0) scale = 1;
			if (scale > 20) scale = 20;
			plat_set_window(windowsize_width(scale), windowsize_height(scale));
		}
		return 0;
	}
	return 1;
}

#define MENU_OPTIONS_GFX \
	mee_cust_s_h  ("Window size",            MA_OPT_VOUT_SIZE, 0,mh_windowsize, mgn_windowsize, NULL), \
	mee_enum_h    ("Aspect ratio",           MA_OPT_ASPECT, currentConfig.aspect, men_aspect_opts, h_aspect), \
	mee_onoff     ("Fullscreen mode",        MA_OPT_VOUT_FULL, plat_target.vout_fullscreen, 1), \
	mee_enum_h    ("Horizontal scaling",     MA_OPT_SCALING, currentConfig.scaling, men_scaling_opts, h_scale), \
	mee_enum_h    ("Vertical scaling",       MA_OPT_VSCALING, currentConfig.vscaling, men_scaling_opts, h_scale), \
	mee_enum_h    ("Scaler type",            MA_OPT3_FILTERING, currentConfig.filter, men_filter_opts, h_stype), \

#define MENU_OPTIONS_ADV

static menu_entry e_menu_keyconfig[], e_menu_gfx_options[];

void linux_menu_init(void)
{
	me_enable(e_menu_gfx_options, MA_OPT_VOUT_SIZE, plat_has_wm());
	me_enable(e_menu_gfx_options, MA_OPT_VOUT_FULL, plat_has_wm());
	me_enable(e_menu_keyconfig, MA_CTRL_DEADZONE, 0);
}

