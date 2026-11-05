
extern const struct in_default_bind *in_sdl_defbinds;
extern const struct menu_keymap *in_sdl_key_map;
extern const int in_sdl_key_map_sz;
extern const struct menu_keymap *in_sdl_joy_map;
extern const int in_sdl_joy_map_sz;
extern const struct menu_keymap *in_sdl_mouse_map;
extern const int in_sdl_mouse_map_sz;
extern const char * const *in_sdl_key_names;
extern const char *plat_device;
extern const struct in_default_bind in_sdl_kbd_map[];

void linux_menu_init(void);

// preset window for an integer line scale (all lines kept)
#define	plat_window_height(scale)	\
	(scale * 240)
// 4:3 -> 320x240, widescreen -> 16:9 width rounded to 4 pixels, matching
// plat_sdl_resize() so a preset survives a WM resize round-trip.
#define	plat_window_width(scale,widescreen)	\
	((widescreen) ? ((320 * (scale) + 1) / 3) * 4 : (scale) * 320)
