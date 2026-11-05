
#ifdef __cplusplus
extern "C" {
#endif

/* What a config file is allowed to set: the global config has everything,
 * the game configs only the game options, so loading a game can't change
 * emulator settings (emu hotkeys, window/screen etc). */
enum cfg_load {
	CFG_LOAD_ALL,   /* every value and every bind: the global config */
	CFG_LOAD_BINDS, /* the game options, plus the global binds */
	CFG_LOAD_GAME,  /* the game options, plus the pad/keyboard binds */
};

int config_write(const char *fname, int game);
int config_writelrom(const char *fname);
int config_readsect(const char *fname, const char *section,
	enum cfg_load load);
void config_reset_game_binds(void);
/* remember the game options the global config left, and put them back
 * before a game config overlays them */
void config_game_opts_capture(void);
void config_game_opts_restore(void);
int config_readlrom(const char *fname);
int config_get_var_val(void *file, char *line, int lsize, char **rvar, char **rval);

#ifdef __cplusplus
}
#endif

