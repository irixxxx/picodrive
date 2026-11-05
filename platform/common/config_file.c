/*
 * Human-readable config file management for PicoDrive
 * (C) notaz, 2008-2010
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __EPOC32__
#include <unistd.h>
#endif

#include "../libpicofe/input.h"
#include "../libpicofe/plat.h"
#include "../libpicofe/lprintf.h"
#include "config_file.h"

static char *mystrip(char *str);

#ifndef _MSC_VER

#include "menu_pico.h"
#include "emu.h"
#include <pico/pico_int.h>

// always output DOS endlines
#ifdef _WIN32
#define NL "\n"
#else
#define NL "\r\n"
#endif

/* Options that describe a game rather than the emulator: game configs
 * read and write only these, so loading a game can't change emulator
 * settings (emu hotkeys, window/screen etc).
 */
static const int cfg_game_opts[] =
{
	// MD/Genesis/Pico options
	MA_OPT_RENDERER,
	MA_OPT2_ENABLE_FM,
	MA_OPT2_FM_CHIP,
	MA_OPT_FM_FILTER,
	MA_OPT_PICO_PEN,
	MA_OPT_ACC_SPRITES,
	MA_OPT2_ENABLE_Z80,
	MA_OPT2_DISABLE_YM_SSG,
	MA_OPT2_ENABLE_SN76496,
	// Sega/Mega CD add-on options
	MA_CDOPT_SAVERAM,
	MA_CDOPT_CDDA,
	MA_CDOPT_PCM,
	MA_CDOPT_SCALEROT_CHIP,
	// 32X add-on options
	MA_32XOPT_ENABLE_32X,
	MA_32XOPT_RENDERER,
	MA_32XOPT_PWM,
	MA_OPT2_PWM_IRQ_OPT,
	MA_32XOPT_H32_LAYER,
	MA_32XOPT_MSH2_CYCLES,
	MA_32XOPT_SSH2_CYCLES,
	// SG/SMS/GG options
	MA_SMSOPT_HARDWARE,
	MA_SMSOPT_MAPPER,
	MA_SMSOPT_GHOSTING,
	MA_OPT2_ENABLE_YM2413,
	MA_SMSOPT_TMSPALETTE,
	// advanced options that affect emulation accuracy
	MA_OPT2_NO_SPRITE_LIM,
	MA_OPT2_NO_IDLE_LOOPS,
	MA_OPT2_ENABLE_GGLCD,
	MA_OPT2_OVERCLOCK_M68K,
	MA_OPT2_DYNARECS,
	// global settings that describe the game rather than the machine
	MA_OPT_WIDESCREEN, // some titles are anamorphic 16:9
	MA_OPT_REGION,
	MA_OPT_INPUT_DEV0,
	MA_OPT_INPUT_DEV1,
	// some games need more clock, others run fine slower and save battery
	MA_OPT_CPU_CLOCKS,
	// each game keeps its own savestate slot for the hotkeys
	MA_OPT_SAVE_SLOT,
	// how a game is shown and how much work it's worth: a title that
	// struggles can skip frames, use a cheaper scaler or a lower sound rate
	MA_OPT_FRAMESKIP,
	MA_OPT2_MAX_FRAMESKIP,
	MA_OPT_ENABLE_SOUND,
	MA_OPT_SOUND_QUALITY,
	MA_OPT_SOUND_FILTER,
	MA_OPT_SOUND_ALPHA,
	MA_OPT_SCALING,
	MA_OPT_VSCALING,
	MA_OPT3_FILTERING,
	0
};

/* the list above is the only place that decides what a game config may
 * set, everything else in the menu is an emulator setting */
static int cfg_is_game_opt(int id)
{
	int i;

	for (i = 0; cfg_game_opts[i]; i++)
		if (cfg_game_opts[i] == id)
			return 1;
	return 0;
}

static int seek_sect(FILE *f, const char *section)
{
	char line[640], *tmp;
	int len;

	len = strlen(section);
	// seek to the section needed
	while (!feof(f))
	{
		tmp = fgets(line, sizeof(line), f);
		if (tmp == NULL) break;

		if (line[0] != '[') continue; // not section start
		if (strncmp(line + 1, section, len) == 0 && line[len+1] == ']')
			return 1; // found it
	}

	return 0;
}

static void keys_write(FILE *fn, int dev_id, const int *binds, const int *kbd_binds,
	int game)
{
	char act[48];
	int key_count = 0, k, i;

	in_get_config(dev_id, IN_CFG_BIND_COUNT, &key_count);

	for (k = 0; k < key_count; k++)
	{
		const char *name;
		int mask;
		act[0] = act[31] = 0;

		name = in_get_key_name(dev_id, k);
		if (strcmp(name, "#") == 0) name = "\\x23"; // replace comment sign
		if (strcmp(name, "=") == 0) name = "\\x3d"; // replace assignment sign

		for (i = 0; me_ctrl_actions[i].name != NULL; i++) {
			mask = me_ctrl_actions[i].mask;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER12)]) {
				strncpy(act, me_ctrl_actions[i].name, 31);
				fprintf(fn, "bind %s = player1 %s" NL, name, mystrip(act));
			}
			mask = me_ctrl_actions[i].mask << 16;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER12)]) {
				strncpy(act, me_ctrl_actions[i].name, 31);
				fprintf(fn, "bind %s = player2 %s" NL, name, mystrip(act));
			}
		}

		for (i = 0; me_ctrl_actions[i].name != NULL; i++) {
			mask = me_ctrl_actions[i].mask;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER34)]) {
				strncpy(act, me_ctrl_actions[i].name, 31);
				fprintf(fn, "bind %s = player3 %s" NL, name, mystrip(act));
			}
			mask = me_ctrl_actions[i].mask << 16;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER34)]) {
				strncpy(act, me_ctrl_actions[i].name, 31);
				fprintf(fn, "bind %s = player4 %s" NL, name, mystrip(act));
			}
		}

		if (game) /* only the pad and keyboard mappings go to a game config */
			continue;

		for (i = 0; emuctrl_actions[i].name != NULL; i++) {
			mask = emuctrl_actions[i].mask;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_EMU)]) {
				strncpy(act, emuctrl_actions[i].name, 31);
				fprintf(fn, "bind %s = %s" NL, name, mystrip(act));
			}
		}
	}

	if (kbd_binds) {
		for (k = 0; k < key_count; k++) {
			const char *name = in_get_key_name(dev_id, k);
			if (strcmp(name, "#") == 0) name = "\\x23"; // replace comment sign
			if (strcmp(name, "=") == 0) name = "\\x3d"; // replace assignment sign
			if (kbd_binds[k])
				fprintf(fn, "bind %s = key%02x" NL, name, kbd_binds[k]);
		}
	}
}

static int binds_have_any(const int *binds, int count)
{
	int i;

	if (binds == NULL)
		return 0;
	for (i = 0; i < count * IN_BINDTYPE_COUNT; i++)
		if (binds[i] != 0)
			return 1;
	return 0;
}

static int kbd_binds_have_any(const int *binds, int count)
{
	int i;

	if (binds == NULL)
		return 0;
	for (i = 0; i < count; i++)
		if (binds[i] != 0)
			return 1;
	return 0;
}

int config_write(const char *fname, int game)
{
	FILE *fn = NULL;
	menu_entry *me;
	int t, k;
	char line[640];

	fn = fopen(fname, "w");
	if (fn == NULL)
		return -1;

	for (me = me_list_get_first(); me != NULL; me = me_list_get_next())
	{
		int dummy;
		if (!me->need_to_save)
			continue;
		if (game && !cfg_is_game_opt(me->id))
			continue;
		if (me->name == NULL || me->name[0] == 0)
			continue;

		if (me->beh == MB_OPT_ONOFF || me->beh == MB_OPT_CUSTONOFF) {
			fprintf(fn, "%s = %i" NL, me->name, (*(int *)me->var & me->mask) ? 1 : 0);
		}
		else if (me->beh == MB_OPT_RANGE || me->beh == MB_OPT_CUSTRANGE) {
			fprintf(fn, "%s = %i" NL, me->name, *(int *)me->var);
		}
		else if (me->beh == MB_OPT_ENUM) {
			const char **names = (const char **)me->data;
			if (names == NULL)
				continue;
			for (t = 0; names[t] != NULL; t++) {
				if (*(int *)me->var == t) {
					strncpy(line, names[t], sizeof(line)-1);
					line[sizeof(line)-1] = '\0';
					goto write_line;
				}
			}
		}
		else if (me->generate_name != NULL) {
			strncpy(line, me->generate_name(me->id, &dummy), sizeof(line)-1);
			line[sizeof(line)-1] = '\0';
			goto write_line;
		}
		else
			lprintf("config: unhandled write: '%s' id %d behavior %d\n",
				me->name, me->id, me->beh);
		continue;

write_line:
		line[sizeof(line) - 1] = 0;
		mystrip(line);
		fprintf(fn, "%s = %s" NL, me->name, line);
	}

	/* Binds are written for both, but a game config gets only the pad and
	 * keyboard mappings; the emu hotkeys stay in the global config. */
	for (t = 0, k = 1; t < IN_MAX_DEVS; t++)
	{
		const int *binds = in_get_dev_binds(t);
		const char *name = in_get_dev_name(t, 0, 0);
		const int *kbd = NULL;
		int count = 0, kk, have;

		if (binds == NULL || name == NULL ||
		    in_get_config(t, IN_CFG_BIND_COUNT, &count) < 0)
			continue;

		if (k) { // make sure keyboard data is only saved once
			kbd = in_get_dev_kbd_binds(t);
			if (kbd_binds_have_any(kbd, count))
				k = 0;
			else
				kbd = NULL;
		}

		/* a game config carries no emu hotkeys, so a device that only
		 * has those doesn't belong in one */
		have = 0;
		if (game) {
			for (kk = 0; kk < count; kk++)
				if (binds[IN_BIND_OFFS(kk, IN_BINDTYPE_PLAYER12)] ||
				    binds[IN_BIND_OFFS(kk, IN_BINDTYPE_PLAYER34)]) {
					have = 1;
					break;
				}
		}
		else
			have = binds_have_any(binds, count);

		if (!have && !kbd_binds_have_any(kbd, count))
			continue;

		fprintf(fn, "binddev = %s" NL, name);
		keys_write(fn, t, binds, kbd, game);
	}

	fprintf(fn, NL);

	fclose(fn);
	return 0;
}

int config_writelrom(const char *fname)
{
	char line[640], *tmp, *optr = NULL;
	char *old_data = NULL;
	int size;
	FILE *f;

	if (strlen(rom_fname_loaded) == 0) return -1;

	f = fopen(fname, "r");
	if (f != NULL)
	{
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		old_data = malloc(size + size/8);
		if (old_data != NULL)
		{
			optr = old_data;
			while (!feof(f))
			{
				tmp = fgets(line, sizeof(line), f);
				if (tmp == NULL) break;
				mystrip(line);
				if (strncasecmp(line, "LastUsedROM", 11) == 0)
					continue;
				sprintf(optr, "%s", line);
				optr += strlen(optr);
			}
		}
		fclose(f);
	}

	f = fopen(fname, "w");
	if (f == NULL) return -1;

	if (old_data != NULL) {
		fwrite(old_data, 1, optr - old_data, f);
		free(old_data);
	}
	fprintf(f, "LastUsedROM = %s" NL, rom_fname_loaded);
	fclose(f);
	return 0;
}

/* --------------------------------------------------------------------------*/

int config_readlrom(const char *fname)
{
	char line[640], *tmp;
	int i, len, ret = -1;
	FILE *f;

	f = fopen(fname, "r");
	if (f == NULL) return -1;

	// seek to the section needed
	while (!feof(f))
	{
		tmp = fgets(line, sizeof(line), f);
		if (tmp == NULL) break;

		if (strncasecmp(line, "LastUsedROM", 11) != 0) continue;
		len = strlen(line);
		for (i = 0; i < len; i++)
			if (line[i] == '#' || line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
		tmp = strchr(line, '=');
		if (tmp == NULL) break;
		tmp++;
		mystrip(tmp);

		len = sizeof(rom_fname_loaded)-1;
		strncpy(rom_fname_loaded, tmp, len);
		rom_fname_loaded[len] = 0;
		ret = 0;
		break;
	}
	fclose(f);
	return ret;
}

static int custom_read(menu_entry *me, const char *var, const char *val)
{
	char *tmp;
	int i;

	switch (me->id)
	{
		case MA_OPT_FRAMESKIP:
			if (strcasecmp(var, "Frameskip") != 0) return 0;
			if (strcasecmp(val, "Auto") == 0)
			     currentConfig.Frameskip = -1;
			else currentConfig.Frameskip = atoi(val);
			return 1;

		case MA_OPT_SOUND_QUALITY:
			if (strcasecmp(var, "Sound Quality") != 0) return 0;
			PicoIn.sndRate = strtoul(val, &tmp, 10);
			if (PicoIn.sndRate < 8000 || PicoIn.sndRate > 54000) {
				if  (strncasecmp(tmp, "native", 6) == 0) {
					tmp += 6;
					PicoIn.sndRate = 53000;
				} else
					PicoIn.sndRate = 22050;
			}
			if (*tmp == 'H' || *tmp == 'h') tmp++;
			if (*tmp == 'Z' || *tmp == 'z') tmp++;
			while (*tmp == ' ') tmp++;
			if        (strcasecmp(tmp, "stereo") == 0) {
				PicoIn.opt |=  POPT_EN_STEREO;
			} else if (strcasecmp(tmp, "mono") == 0) {
				PicoIn.opt &= ~POPT_EN_STEREO;
			} else
				return 0;
			return 1;

		case MA_OPT_SOUND_ALPHA:
			if (strcasecmp(var, "Filter strength") != 0) return 0;
			PicoIn.sndFilterAlpha = 0x10000 * atof(val);
			return 1;

		case MA_OPT_REGION:
			if (strcasecmp(var, "Region") != 0) return 0;
			while (*val == ' ') val++;
			if       (strncasecmp(val, "Auto:", 5) == 0)
			{
				const char *p = val + 5, *end = val + strlen(val);
				int i;
				PicoIn.regionOverride = PicoIn.autoRgnOrder = 0;
				for (i = 0; p < end && i < 3; i++)
				{
					while (*p == ' ') p++;
					if        (p[0] == 'J' && p[1] == 'P') {
						PicoIn.autoRgnOrder |= 1 << (i*4);
					} else if (p[0] == 'U' && p[1] == 'S') {
						PicoIn.autoRgnOrder |= 4 << (i*4);
					} else if (p[0] == 'E' && p[1] == 'U') {
						PicoIn.autoRgnOrder |= 8 << (i*4);
					}
					while (*p != ' ' && *p != 0) p++;
					if (*p == 0) break;
				}
			}
			else   if (strcasecmp(val, "Auto") == 0) {
				PicoIn.regionOverride = 0;
			} else if (strcasecmp(val, "Japan NTSC") == 0) {
				PicoIn.regionOverride = 1;
			} else if (strcasecmp(val, "Japan PAL") == 0) {
				PicoIn.regionOverride = 2;
			} else if (strcasecmp(val, "USA") == 0) {
				PicoIn.regionOverride = 4;
			} else if (strcasecmp(val, "Europe") == 0) {
				PicoIn.regionOverride = 8;
			} else
				return 0;
			return 1;

		case MA_32XOPT_MSH2_CYCLES:
			currentConfig.msh2_khz = atoi(val);
			return 1;

		case MA_32XOPT_SSH2_CYCLES:
			currentConfig.ssh2_khz = atoi(val);
			return 1;

		case MA_OPT_KEYBOARD:
			currentConfig.keyboard = 0;
			if (strcasecmp(val, "physical") == 0)
				currentConfig.keyboard = 2;
			else if (strcasecmp(val, "virtual") == 0)
				currentConfig.keyboard = 1;
			return 1;

		case MA_OPT_INPUT_DEV0:
			for (i = 0; indev_names[i]; i++)
				if (strcasecmp(val, indev_names[i]) == 0)
					currentConfig.input_dev[0] = i;
			return 1;
		case MA_OPT_INPUT_DEV1:
			for (i = 0; indev_names[i]; i++)
				if (strcasecmp(val, indev_names[i]) == 0)
					currentConfig.input_dev[1] = i;
			return 1;
		case MA_OPT2_FM_CHIP:
			PicoIn.opt &= ~POPT_FM_YM2612;
			if (strcasecmp(val, "ym2612") == 0)
				PicoIn.opt |= POPT_FM_YM2612;
			return 1;

		/* PSP */
		case MA_OPT3_VSYNC:
			// XXX: use enum
			if (strcasecmp(var, "Wait for vsync") != 0) return 0;
			if        (strcasecmp(val, "never") == 0) {
				currentConfig.EmuOpt &= ~(EOPT_VSYNC|EOPT_VSYNC_MODE);
			} else if (strcasecmp(val, "sometimes") == 0) {
				currentConfig.EmuOpt |=  (EOPT_VSYNC|EOPT_VSYNC_MODE);
			} else if (strcasecmp(val, "always") == 0) {
				currentConfig.EmuOpt &= ~EOPT_VSYNC_MODE;
				currentConfig.EmuOpt |=  EOPT_VSYNC;
			} else
				return 0;
			return 1;

		default:
			lprintf("unhandled custom_read %i: %s\n", me->id, var);
			return 0;
	}
}

static int parse_bind_val(const char *val, int *type)
{
	int i;

	*type = IN_BINDTYPE_NONE;
	if (val[0] == 0)
		return 0;
	
	if (strncasecmp(val, "key", 3) == 0)
	{
		*type = IN_BINDTYPE_KEYBOARD;
		return strtol(val + 3, NULL, 16);
	}

	if (strncasecmp(val, "player", 6) == 0)
	{
		int player, shift = 0;
		player = atoi(val + 6) - 1;

		if (player > 3)
			return -1;
		if (player & 1)
			shift = 16;

		*type = IN_BINDTYPE_PLAYER12 + (player >> 1);
		for (i = 0; me_ctrl_actions[i].name != NULL; i++) {
			if (strncasecmp(me_ctrl_actions[i].name, val + 8, strlen(val + 8)) == 0)
				return me_ctrl_actions[i].mask << shift;
		}
	}
	for (i = 0; emuctrl_actions[i].name != NULL; i++) {
		if (strncasecmp(emuctrl_actions[i].name, val, strlen(val)) == 0) {
			*type = IN_BINDTYPE_EMU;
			return emuctrl_actions[i].mask;
		}
	}

	return -1;
}

/* Mask of all the pad actions, to clear only those binds. The high half
 * covers player2/4, which share a value with player1/3. */
static int pad_binds_mask(void)
{
	int i, mask = 0;

	for (i = 0; me_ctrl_actions[i].name != NULL; i++)
		mask |= me_ctrl_actions[i].mask | (me_ctrl_actions[i].mask << 16);
	return mask;
}

static void kbd_binds_clear(int dev_id)
{
	int count = 0, k;

	if (in_get_dev_kbd_binds(dev_id) == NULL)
		return;
	if (in_get_config(dev_id, IN_CFG_BIND_COUNT, &count) < 0)
		return;

	for (k = 0; k < count; k++)
		in_bind_kbd_key(dev_id, k, -1);
}

/* Save global keyboard mappings; unlike pad binds, they are not reset before
 * a game load. */
static int *cfg_kbd_base[IN_MAX_DEVS];
static int cfg_kbd_base_count[IN_MAX_DEVS];
static int cfg_kbd_base_initialized;

static void kbd_base_capture(void)
{
	int dev, count;

	if (!cfg_kbd_base_initialized) {
		for (dev = 0; dev < IN_MAX_DEVS; dev++)
			cfg_kbd_base_count[dev] = -1;
		cfg_kbd_base_initialized = 1;
	}

	for (dev = 0; dev < IN_MAX_DEVS; dev++) {
		const int *kbd;

		free(cfg_kbd_base[dev]);
		cfg_kbd_base[dev] = NULL;
		cfg_kbd_base_count[dev] = -1;

		if (in_get_dev_name(dev, 1, 0) == NULL)
			continue;
		if (in_get_config(dev, IN_CFG_BIND_COUNT, &count) < 0 || count <= 0)
			continue;

		kbd = in_get_dev_kbd_binds(dev);
		if (kbd == NULL) {
			cfg_kbd_base_count[dev] = 0;
			continue;
		}

		cfg_kbd_base[dev] = malloc(count * sizeof(*kbd));
		if (cfg_kbd_base[dev] == NULL)
			continue;
		memcpy(cfg_kbd_base[dev], kbd, count * sizeof(*kbd));
		cfg_kbd_base_count[dev] = count;
	}
}

static void kbd_base_restore(void)
{
	int dev, count, i;

	if (!cfg_kbd_base_initialized)
		return;

	for (dev = 0; dev < IN_MAX_DEVS; dev++) {
		if (cfg_kbd_base_count[dev] < 0)
			continue;
		if (in_get_dev_name(dev, 1, 0) == NULL)
			continue;
		if (in_get_config(dev, IN_CFG_BIND_COUNT, &count) < 0 || count <= 0)
			continue;

		for (i = 0; i < count; i++) {
			int key = i < cfg_kbd_base_count[dev] ?
				cfg_kbd_base[dev][i] : -1;
			in_bind_kbd_key(dev, i, key);
		}
	}
}

/* Game options live in currentConfig, in PicoIn and in a few globals. The
 * snapshot holds the values the global config left, so a game load can put
 * them back and let the game config override only what it mentions.
 * Emulator options are never part of it. */
static struct {
	currentConfig_t cfg;
	int picoopt, snd_rate, snd_alpha, hw_select, mapper, tms_palette;
	int region, rgn_order, slot;
} cfg_game_base;
static int cfg_game_base_valid;

static void game_opt_xfer(menu_entry *me, int save)
{
	int onoff = me->beh == MB_OPT_ONOFF || me->beh == MB_OPT_CUSTONOFF;
	void *var = me->var;
	int *cur = me->var;

	if (var == NULL) {
		/* custom entries: the value is not a plain config field */
		switch (me->id) {
		case MA_OPT_SOUND_QUALITY:
			if (save) {
				cfg_game_base.snd_rate = PicoIn.sndRate;
				cfg_game_base.picoopt = (cfg_game_base.picoopt & ~POPT_EN_STEREO) |
					(PicoIn.opt & POPT_EN_STEREO);
			} else {
				PicoIn.sndRate = cfg_game_base.snd_rate;
				PicoIn.opt = (PicoIn.opt & ~POPT_EN_STEREO) |
					(cfg_game_base.picoopt & POPT_EN_STEREO);
			}
			break;
		case MA_OPT_SOUND_ALPHA:
			if (save) cfg_game_base.snd_alpha = PicoIn.sndFilterAlpha;
			else      PicoIn.sndFilterAlpha = cfg_game_base.snd_alpha;
			break;
		case MA_OPT_REGION:
			if (save) {
				cfg_game_base.region = PicoIn.regionOverride;
				cfg_game_base.rgn_order = PicoIn.autoRgnOrder;
			} else {
				PicoIn.regionOverride = cfg_game_base.region;
				PicoIn.autoRgnOrder = cfg_game_base.rgn_order;
			}
			break;
		case MA_OPT_INPUT_DEV0:
			if (save) cfg_game_base.cfg.input_dev[0] = currentConfig.input_dev[0];
			else      currentConfig.input_dev[0] = cfg_game_base.cfg.input_dev[0];
			break;
		case MA_OPT_INPUT_DEV1:
			if (save) cfg_game_base.cfg.input_dev[1] = currentConfig.input_dev[1];
			else      currentConfig.input_dev[1] = cfg_game_base.cfg.input_dev[1];
			break;
		case MA_32XOPT_MSH2_CYCLES:
			if (save) cfg_game_base.cfg.msh2_khz = currentConfig.msh2_khz;
			else      currentConfig.msh2_khz = cfg_game_base.cfg.msh2_khz;
			break;
		case MA_32XOPT_SSH2_CYCLES:
			if (save) cfg_game_base.cfg.ssh2_khz = currentConfig.ssh2_khz;
			else      currentConfig.ssh2_khz = cfg_game_base.cfg.ssh2_khz;
			break;
		case MA_OPT2_FM_CHIP: /* also a plain onoff entry elsewhere */
			if (save)
				cfg_game_base.picoopt = (cfg_game_base.picoopt & ~POPT_FM_YM2612) |
					(PicoIn.opt & POPT_FM_YM2612);
			else
				PicoIn.opt = (PicoIn.opt & ~POPT_FM_YM2612) |
					(cfg_game_base.picoopt & POPT_FM_YM2612);
			break;
		default:
			break;
		}
		return;
	}

	if (var == &PicoIn.opt) {
		if (!onoff)
			return;
		if (save)
			cfg_game_base.picoopt = (cfg_game_base.picoopt & ~me->mask) |
				(PicoIn.opt & me->mask);
		else
			PicoIn.opt = (PicoIn.opt & ~me->mask) |
				(cfg_game_base.picoopt & me->mask);
		return;
	}
	if (var == &PicoIn.hwSelect) {
		if (save) cfg_game_base.hw_select = PicoIn.hwSelect;
		else      PicoIn.hwSelect = cfg_game_base.hw_select;
		return;
	}
	if (var == &PicoIn.mapper) {
		if (save) cfg_game_base.mapper = PicoIn.mapper;
		else      PicoIn.mapper = cfg_game_base.mapper;
		return;
	}
	if (var == &PicoIn.tmsPalette) {
		if (save) cfg_game_base.tms_palette = PicoIn.tmsPalette;
		else      PicoIn.tmsPalette = cfg_game_base.tms_palette;
		return;
	}
	if (var == &state_slot) {
		if (save) cfg_game_base.slot = state_slot;
		else      state_slot = cfg_game_base.slot;
		return;
	}

	/* everything else lives in currentConfig, the snapshot mirrors it */
	if ((char *)cur >= (char *)&currentConfig &&
	    (char *)cur <  (char *)&currentConfig + sizeof(currentConfig)) {
		int *base = (int *)((char *)&cfg_game_base.cfg +
			((char *)cur - (char *)&currentConfig));

		if (onoff) {
			if (save) *base = (*base & ~me->mask) | (*cur & me->mask);
			else      *cur  = (*cur & ~me->mask) | (*base & me->mask);
		} else if (save)
			*base = *cur;
		else
			*cur = *base;
	}
}

/* Save global game options and keyboard mappings so game loads start from
 * the global values instead of the previous game's. */
void config_game_opts_capture(void)
{
	menu_entry *me;

	memset(&cfg_game_base, 0, sizeof(cfg_game_base));
	for (me = me_list_get_first(); me != NULL; me = me_list_get_next())
		if (me->need_to_save && cfg_is_game_opt(me->id))
			game_opt_xfer(me, 1);
	cfg_game_base_valid = 1;
	kbd_base_capture();
}

/* Restore global game options and keyboard mappings before game configs
 * overlay them; emulator options are unaffected. */
void config_game_opts_restore(void)
{
	menu_entry *me;

	if (!cfg_game_base_valid)
		return;
	for (me = me_list_get_first(); me != NULL; me = me_list_get_next())
		if (me->need_to_save && cfg_is_game_opt(me->id))
			game_opt_xfer(me, 0);
	kbd_base_restore();
}

/* Reset pad mappings to device defaults so a game config does not inherit
 * the previous game's binds. Emu hotkeys stay; keyboard binds are restored
 * separately. */
void config_reset_game_binds(void)
{
	int dev, count, key, len;
	const int *binds, *defs;
	int *defs_copy;

	for (dev = 0; dev < IN_MAX_DEVS; dev++) {
		if (in_get_dev_name(dev, 1, 0) == NULL)
			continue;
		binds = in_get_dev_binds(dev);
		defs = in_get_dev_def_binds(dev);
		if (binds == NULL || defs == NULL)
			continue;
		if (in_get_config(dev, IN_CFG_BIND_COUNT, &count) < 0 || count <= 0)
			continue;

		/* the defaults sit in the same allocation as dev->binds, which
		 * in_bind_key() frees once a device is left without binds, so
		 * replay them from a private copy */
		len = count * IN_BINDTYPE_COUNT;
		defs_copy = malloc(len * sizeof(*defs_copy));
		if (defs_copy == NULL)
			continue;
		memcpy(defs_copy, defs, len * sizeof(*defs_copy));

		in_unbind_all(dev, pad_binds_mask(), IN_BINDTYPE_PLAYER12);
		in_unbind_all(dev, pad_binds_mask(), IN_BINDTYPE_PLAYER34);

		for (key = 0; key < count; key++) {
			int mask;

			mask = defs_copy[IN_BIND_OFFS(key, IN_BINDTYPE_PLAYER12)];
			if (mask)
				in_bind_key(dev, key, mask, IN_BINDTYPE_PLAYER12, 0);

			mask = defs_copy[IN_BIND_OFFS(key, IN_BINDTYPE_PLAYER34)];
			if (mask)
				in_bind_key(dev, key, mask, IN_BINDTYPE_PLAYER34, 0);
		}
		free(defs_copy);
	}
}

static void clear_bind_type(int dev_id, int type, int game)
{
	switch (type) {
	case IN_BINDTYPE_PLAYER12:
		in_unbind_all(dev_id, pad_binds_mask(), IN_BINDTYPE_PLAYER12);
		break;
	case IN_BINDTYPE_PLAYER34:
		in_unbind_all(dev_id, pad_binds_mask(), IN_BINDTYPE_PLAYER34);
		break;
	case IN_BINDTYPE_EMU:
		in_unbind_all(dev_id, 0x7fffffff, IN_BINDTYPE_EMU);
		break;
	case IN_BINDTYPE_KEYBOARD:
		kbd_binds_clear(dev_id);
		break;
	default:
		/* an empty binding clears the whole device */
		in_unbind_all(dev_id, pad_binds_mask(), IN_BINDTYPE_PLAYER12);
		in_unbind_all(dev_id, pad_binds_mask(), IN_BINDTYPE_PLAYER34);
		kbd_binds_clear(dev_id);
		if (!game)
			in_unbind_all(dev_id, 0x7fffffff, IN_BINDTYPE_EMU);
		break;
	}
}

static void keys_parse_all(FILE *f, int game)
{
	char line[640], *var, *val;
	int dev_id = -1;
	int dev_cleared = 0;
	int acts, type;
	int ret;

	while (!feof(f))
	{
		ret = config_get_var_val(f, line, sizeof(line), &var, &val);
		if (ret ==  0) break;
		if (ret == -1) continue;

		if (strcasecmp(var, "binddev") == 0) {
			dev_id = in_config_parse_dev(val);
			dev_cleared = 0;
			if (dev_id < 0) {
				printf("input: can't handle dev: %s\n", val);
				continue;
			}
			continue;
		}
		if (dev_id < 0 || strncasecmp(var, "bind ", 5) != 0)
			continue;

		acts = parse_bind_val(val, &type);
		if (acts == -1) {
			lprintf("config: unhandled action \"%s\"\n", val);
			continue;
		}
		if (game && type == IN_BINDTYPE_EMU) {
			lprintf("config: emu hotkey in game config: \"%s\"\n", val);
			continue;
		}

		/* Clear only when the section actually has a usable binding,
		 * and only the bind type being replaced. A bare binddev or a
		 * section with no valid binds must leave the defaults (or the
		 * global mapping) in place. */
		if (type == IN_BINDTYPE_NONE) {
			clear_bind_type(dev_id, type, game);
			dev_cleared = ~0;
		}
		else if (!(dev_cleared & (1 << type))) {
			clear_bind_type(dev_id, type, game);
			dev_cleared |= 1 << type;
		}

		mystrip(var + 5);
		if (type == IN_BINDTYPE_KEYBOARD)
			in_config_bind_kbd_key(dev_id, var + 5, acts);
		else
			in_config_bind_key(dev_id, var + 5, acts, type);
	}
	in_clean_binds();
}

static void parse(const char *var, const char *val, int *keys_encountered,
	const int *opts)
{
	menu_entry *me;
	int matched = 0;
	int i, tmp;

	if (strcasecmp(var, "LastUsedROM") == 0)
		return; /* handled elsewhere */

	if (strncasecmp(var, "bind", 4) == 0) {
		*keys_encountered = 1;
		return; /* handled elsewhere */
	}

	for (me = me_list_get_first(); me != NULL; me = me_list_get_next())
	{
		char *p;

		if (!me->need_to_save)
			continue;
		if (me->name == NULL || strcasecmp(var, me->name) != 0)
			continue;
		matched = 1;
		if (opts != NULL && !cfg_is_game_opt(me->id))
			continue; /* not a game option, another entry may match */
		if (me->beh == MB_OPT_ONOFF || me->beh == MB_OPT_CUSTONOFF) {
			tmp = strtol(val, &p, 0);
			if (*p != 0)
				goto bad_val;
			if (tmp) *(int *)me->var |=  me->mask;
			else     *(int *)me->var &= ~me->mask;
			return;
		}
		else if (me->beh == MB_OPT_RANGE || me->beh == MB_OPT_CUSTRANGE) {
			tmp = strtol(val, &p, 0);
			if (*p != 0)
				goto bad_val;
			if (tmp < me->min) tmp = me->min;
			if (tmp > me->max) tmp = me->max;
			*(int *)me->var = tmp;
			return;
		}
		else if (me->beh == MB_OPT_ENUM) {
			const char **names, *p1;

			names = (const char **)me->data;
			if (names == NULL)
				goto bad_val;
			for (i = 0; names[i] != NULL; i++) {
				for (p1 = names[i]; *p1 == ' '; p1++)
					;
				if (strcasecmp(p1, val) == 0) {
					*(int *)me->var = i;
					return;
				}
			}
			goto bad_val;
		}
		else if (custom_read(me, var, val))
			return;
	}

	/* when only the game options are wanted, a known emulator
	 * setting is intentionally ignored, not an error */
	if (matched && opts != NULL)
		return;

	lprintf("config_readsect: unhandled var: \"%s\"\n", var);
	return;

bad_val:
	lprintf("config_readsect: unhandled val for \"%s\": \"%s\"\n", var, val);
}

int config_readsect(const char *fname, const char *section, enum cfg_load load)
{
	const int *opts = load == CFG_LOAD_ALL ? NULL : cfg_game_opts;
	int game = load == CFG_LOAD_GAME;
	char line[640], *var, *val;
	int keys_encountered = 0;
	FILE *f;
	int ret;

	/* the global config starts from the defaults, the game configs only
	 * overlay the game options on the live values. A game load also
	 * re-reads the global file, but just for the game options and the
	 * global binds. */
	if (load == CFG_LOAD_ALL)
		emu_set_defconfig();

	f = fopen(fname, "r");
	if (f == NULL) return -1;

	if (section != NULL)
	{
		/* for game_def.cfg only */
		ret = seek_sect(f, section);
		if (!ret) {
			lprintf("config_readsect: %s: missing section [%s]\n", fname, section);
			fclose(f);
			return -1;
		}
	}

	while (!feof(f))
	{
		ret = config_get_var_val(f, line, sizeof(line), &var, &val);
		if (ret ==  0) break;
		if (ret == -1) continue;

		parse(var, val, &keys_encountered, opts);
	}

	if (keys_encountered) {
		rewind(f);
		keys_parse_all(f, game);
	}

	fclose(f);

	lprintf("config_readsect: loaded from %s", fname);
	if (section != NULL)
		lprintf(" [%s]", section);
	printf("\n");

	return 0;
}

#endif // _MSC_VER

static char *mystrip(char *str)
{
	int i, len;

	len = strlen(str);
	for (i = 0; i < len; i++)
		if (str[i] != ' ') break;
	if (i > 0) memmove(str, str + i, len - i + 1);

	len = strlen(str);
	for (i = len - 1; i >= 0; i--)
		if (str[i] != ' ') break;
	str[i+1] = 0;

	return str;
}

/* returns:
 *  0 - EOF, end
 *  1 - parsed ok
 * -1 - failed to parse line
 */
int config_get_var_val(void *file, char *line, int lsize, char **rvar, char **rval)
{
	char *var, *val, *tmp;
	FILE *f = file;
	int len, i;

	tmp = fgets(line, lsize, f);
	if (tmp == NULL) return 0;

	if (line[0] == '[') return 0; // other section

	// strip comments, linefeed, spaces..
	len = strlen(line);
	for (i = 0; i < len; i++)
		if (line[i] == '#' || line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
	mystrip(line);
	len = strlen(line);
	if (len <= 0) return -1;;

	// get var and val
	for (i = 0; i < len; i++)
		if (line[i] == '=') break;
	if (i >= len || strchr(&line[i+1], '=') != NULL) {
		lprintf("config_readsect: can't parse: %s\n", line);
		return -1;
	}
	line[i] = 0;
	var = line;
	val = &line[i+1];
	mystrip(var);
	mystrip(val);

#ifndef _MSC_VER
	if (strlen(var) == 0 || (strlen(val) == 0 && strncasecmp(var, "bind", 4) != 0)) {
		lprintf("config_readsect: something's empty: \"%s\" = \"%s\"\n", var, val);
		return -1;;
	}
#endif

	*rvar = var;
	*rval = val;
	return 1;
}

