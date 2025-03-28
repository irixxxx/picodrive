#include <stdlib.h>
#include <SDL_keysym.h>

#include "../libpicofe/input.h"
#include "../libpicofe/in_sdl.h"
#include "../libpicofe/plat.h"
#include "../common/input_pico.h"
#include "../common/plat_sdl.h"

const struct in_default_bind _in_sdl_defbinds[] = {
	{ SDLK_UP,     IN_BINDTYPE_PLAYER12, GBTN_UP },
	{ SDLK_DOWN,   IN_BINDTYPE_PLAYER12, GBTN_DOWN },
	{ SDLK_LEFT,   IN_BINDTYPE_PLAYER12, GBTN_LEFT },
	{ SDLK_RIGHT,  IN_BINDTYPE_PLAYER12, GBTN_RIGHT },
	{ SDLK_z,      IN_BINDTYPE_PLAYER12, GBTN_A },
	{ SDLK_x,      IN_BINDTYPE_PLAYER12, GBTN_B },
	{ SDLK_c,      IN_BINDTYPE_PLAYER12, GBTN_C },
	{ SDLK_a,      IN_BINDTYPE_PLAYER12, GBTN_X },
	{ SDLK_s,      IN_BINDTYPE_PLAYER12, GBTN_Y },
	{ SDLK_d,      IN_BINDTYPE_PLAYER12, GBTN_Z },
	{ SDLK_RETURN, IN_BINDTYPE_PLAYER12, GBTN_START },
	{ SDLK_f,      IN_BINDTYPE_PLAYER12, GBTN_MODE },
	{ SDLK_ESCAPE, IN_BINDTYPE_EMU, PEVB_MENU },
	{ SDLK_TAB,    IN_BINDTYPE_EMU, PEVB_RESET },
	{ SDLK_F1,     IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
	{ SDLK_F2,     IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
	{ SDLK_F3,     IN_BINDTYPE_EMU, PEVB_SSLOT_PREV },
	{ SDLK_F4,     IN_BINDTYPE_EMU, PEVB_SSLOT_NEXT },
	{ SDLK_F5,     IN_BINDTYPE_EMU, PEVB_SWITCH_RND },
	{ SDLK_F6,     IN_BINDTYPE_EMU, PEVB_PICO_PPREV },
	{ SDLK_F7,     IN_BINDTYPE_EMU, PEVB_PICO_PNEXT },
	{ SDLK_F8,     IN_BINDTYPE_EMU, PEVB_PICO_STORY },
	{ SDLK_F9,     IN_BINDTYPE_EMU, PEVB_PICO_PAD },
	{ SDLK_F10,    IN_BINDTYPE_EMU, PEVB_GRAB_INPUT },
	{ SDLK_F12,    IN_BINDTYPE_EMU, PEVB_SWITCH_KBD },
	{ SDLK_BACKSPACE, IN_BINDTYPE_EMU, PEVB_FF },

	{ 0, 0, 0 }
};

// use something close to a US keyboard as default, swapping some keys around
// to feel more natural (if that's possible at all :-/)
// TODO should backquote and backslash be swapped on "yen" and "at"?
const struct in_default_bind in_sdl_kbd_map[] = {
	// Blue buttons
	{ SDLK_1, IN_BINDTYPE_KEYBOARD, PEVB_KBD_1 },
	{ SDLK_2, IN_BINDTYPE_KEYBOARD, PEVB_KBD_2 },
	{ SDLK_3, IN_BINDTYPE_KEYBOARD, PEVB_KBD_3 },
	{ SDLK_4, IN_BINDTYPE_KEYBOARD, PEVB_KBD_4 },
	{ SDLK_5, IN_BINDTYPE_KEYBOARD, PEVB_KBD_5 },
	{ SDLK_6, IN_BINDTYPE_KEYBOARD, PEVB_KBD_6 },
	{ SDLK_7, IN_BINDTYPE_KEYBOARD, PEVB_KBD_7 },
	{ SDLK_8, IN_BINDTYPE_KEYBOARD, PEVB_KBD_8 },
	{ SDLK_9, IN_BINDTYPE_KEYBOARD, PEVB_KBD_9 },
	{ SDLK_0, IN_BINDTYPE_KEYBOARD, PEVB_KBD_0 },
	{ SDLK_MINUS, IN_BINDTYPE_KEYBOARD, PEVB_KBD_MINUS },
	{ SDLK_EQUALS, IN_BINDTYPE_KEYBOARD, PEVB_KBD_CARET },
	{ SDLK_BACKQUOTE, IN_BINDTYPE_KEYBOARD, PEVB_KBD_YEN },

	{ SDLK_q, IN_BINDTYPE_KEYBOARD, PEVB_KBD_q },
	{ SDLK_w, IN_BINDTYPE_KEYBOARD, PEVB_KBD_w },
	{ SDLK_e, IN_BINDTYPE_KEYBOARD, PEVB_KBD_e },
	{ SDLK_r, IN_BINDTYPE_KEYBOARD, PEVB_KBD_r },
	{ SDLK_t, IN_BINDTYPE_KEYBOARD, PEVB_KBD_t },
	{ SDLK_y, IN_BINDTYPE_KEYBOARD, PEVB_KBD_y },
	{ SDLK_u, IN_BINDTYPE_KEYBOARD, PEVB_KBD_u },
	{ SDLK_i, IN_BINDTYPE_KEYBOARD, PEVB_KBD_i },
	{ SDLK_o, IN_BINDTYPE_KEYBOARD, PEVB_KBD_o },
	{ SDLK_p, IN_BINDTYPE_KEYBOARD, PEVB_KBD_p },
	{ SDLK_BACKSLASH, IN_BINDTYPE_KEYBOARD, PEVB_KBD_AT },
	{ SDLK_LEFTBRACKET, IN_BINDTYPE_KEYBOARD, PEVB_KBD_LEFTBRACKET },

	{ SDLK_a, IN_BINDTYPE_KEYBOARD, PEVB_KBD_a },
	{ SDLK_s, IN_BINDTYPE_KEYBOARD, PEVB_KBD_s },
	{ SDLK_d, IN_BINDTYPE_KEYBOARD, PEVB_KBD_d },
	{ SDLK_f, IN_BINDTYPE_KEYBOARD, PEVB_KBD_f },
	{ SDLK_g, IN_BINDTYPE_KEYBOARD, PEVB_KBD_g },
	{ SDLK_h, IN_BINDTYPE_KEYBOARD, PEVB_KBD_h },
	{ SDLK_j, IN_BINDTYPE_KEYBOARD, PEVB_KBD_j },
	{ SDLK_k, IN_BINDTYPE_KEYBOARD, PEVB_KBD_k },
	{ SDLK_l, IN_BINDTYPE_KEYBOARD, PEVB_KBD_l },
	{ SDLK_SEMICOLON, IN_BINDTYPE_KEYBOARD, PEVB_KBD_SEMICOLON },
	{ SDLK_QUOTE, IN_BINDTYPE_KEYBOARD, PEVB_KBD_COLON },
	{ SDLK_RIGHTBRACKET, IN_BINDTYPE_KEYBOARD, PEVB_KBD_RIGHTBRACKET },

	{ SDLK_z, IN_BINDTYPE_KEYBOARD, PEVB_KBD_z },
	{ SDLK_x, IN_BINDTYPE_KEYBOARD, PEVB_KBD_x },
	{ SDLK_c, IN_BINDTYPE_KEYBOARD, PEVB_KBD_c },
	{ SDLK_v, IN_BINDTYPE_KEYBOARD, PEVB_KBD_v },
	{ SDLK_b, IN_BINDTYPE_KEYBOARD, PEVB_KBD_b },
	{ SDLK_n, IN_BINDTYPE_KEYBOARD, PEVB_KBD_n },
	{ SDLK_m, IN_BINDTYPE_KEYBOARD, PEVB_KBD_m },
	{ SDLK_COMMA, IN_BINDTYPE_KEYBOARD, PEVB_KBD_COMMA },
	{ SDLK_PERIOD, IN_BINDTYPE_KEYBOARD, PEVB_KBD_PERIOD },
	{ SDLK_SLASH, IN_BINDTYPE_KEYBOARD, PEVB_KBD_SLASH },
	{ SDLK_RSHIFT, IN_BINDTYPE_KEYBOARD, PEVB_KBD_RO },

	{ SDLK_SPACE, IN_BINDTYPE_KEYBOARD, PEVB_KBD_SPACE },

	// Green button on top-left
	{ SDLK_ESCAPE, IN_BINDTYPE_KEYBOARD, PEVB_KBD_ESCAPE },

	// Orange buttons on left
	// NB caps lock generates only a pulse on most keyboards, even if the
	// button is kept pressed. Using ctrl key for complete up/down handling.
	{ SDLK_LCTRL, IN_BINDTYPE_KEYBOARD, PEVB_KBD_CAPSLOCK }, // Also switches english input
	{ SDLK_LSHIFT, IN_BINDTYPE_KEYBOARD, PEVB_KBD_LSHIFT },

	// Green buttons on right
	{ SDLK_BACKSPACE, IN_BINDTYPE_KEYBOARD, PEVB_KBD_BACKSPACE },
	{ SDLK_INSERT, IN_BINDTYPE_KEYBOARD, PEVB_KBD_INSERT },
	{ SDLK_DELETE, IN_BINDTYPE_KEYBOARD, PEVB_KBD_DELETE },

	// Red button on bottom-right
	{ SDLK_RETURN, IN_BINDTYPE_KEYBOARD, PEVB_KBD_RETURN },

	// Orange buttons on bottom
	{ SDLK_END, IN_BINDTYPE_KEYBOARD, PEVB_KBD_SOUND },
	{ SDLK_HOME, IN_BINDTYPE_KEYBOARD, PEVB_KBD_HOME },
	{ SDLK_PAGEUP, IN_BINDTYPE_KEYBOARD, PEVB_KBD_CJK }, // CJK Scripts: Hiragana / Katakana / Kanji (Keyboard Pico); Hangul (Kibodeu Piko)
	{ SDLK_PAGEDOWN, IN_BINDTYPE_KEYBOARD, PEVB_KBD_ROMAJI }, // English Script

	// Others for SC-3000
	{ SDLK_LALT, IN_BINDTYPE_KEYBOARD, PEVB_KBD_FUNC },
	{ SDLK_UP, IN_BINDTYPE_KEYBOARD, PEVB_KBD_UP },
	{ SDLK_DOWN, IN_BINDTYPE_KEYBOARD, PEVB_KBD_DOWN },
	{ SDLK_LEFT, IN_BINDTYPE_KEYBOARD, PEVB_KBD_LEFT },
	{ SDLK_RIGHT, IN_BINDTYPE_KEYBOARD, PEVB_KBD_RIGHT },

	{ 0, 0, 0 }
};
const struct in_default_bind *in_sdl_defbinds = _in_sdl_defbinds;

const struct menu_keymap _in_sdl_key_map[] = {
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	{ SDLK_RETURN,	PBTN_MOK },
	{ SDLK_ESCAPE,	PBTN_MBACK },
	{ SDLK_COMMA,	PBTN_MA2 },
	{ SDLK_PERIOD,	PBTN_MA3 },
	{ SDLK_TAB,	PBTN_R },
	{ SDLK_BACKSPACE, PBTN_L },
};
const int in_sdl_key_map_sz = sizeof(_in_sdl_key_map) / sizeof(_in_sdl_key_map[0]);
const struct menu_keymap *in_sdl_key_map = _in_sdl_key_map;

const struct menu_keymap _in_sdl_joy_map[] = {
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	/* joystick */
	{ SDLK_WORLD_0,	PBTN_MOK },
	{ SDLK_WORLD_1,	PBTN_MBACK },
	{ SDLK_WORLD_2,	PBTN_MA2 },
	{ SDLK_WORLD_3,	PBTN_MA3 },
};
const int in_sdl_joy_map_sz = sizeof(_in_sdl_joy_map) / sizeof(_in_sdl_joy_map[0]);
const struct menu_keymap *in_sdl_joy_map = _in_sdl_joy_map;

const char * const *in_sdl_key_names = NULL;

void plat_target_setup_input(void)
{
}
