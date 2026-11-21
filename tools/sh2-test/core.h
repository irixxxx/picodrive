/*
 * Shared definitions for the external sh2 single-step test harness.
 *
 * The harness runs the SingleStepTests/sh4 vectors against the MAME SH-2
 * interpreter in cpu/sh2/mame.  The vectors carry the SH-4 register set, so
 * the state keeps every field they provide even though the SH-2 has no FPU
 * and no DBR/SGR/SSR/SPC.
 */
#ifndef SH2_TEST_CORE_H
#define SH2_TEST_CORE_H

#include <stdint.h>

/* One architectural state as stored in the vectors. */
struct sh2_state {
	uint32_t r[16];
	uint32_t rbank[8];	/* R_ , the bank SR.RB selects */
	uint32_t pc, gbr, sr, ssr, spc, vbr, sgr, dbr;
	uint32_t macl, mach, pr, fpscr, fpul;
};

/* One recorded bus cycle of the reference run.  actions is a bitmask:
 * 1 read, 2 write, 4 instruction fetch. */
struct sh2_cycle {
	uint32_t actions;
	uint32_t fetch_addr;
	uint32_t write_addr, write_size;
	uint32_t read_addr, read_size;
	uint64_t write_val, read_val;
};

#define SH2_TEST_OPCODES 5

struct sh2_test {
	char name[80];
	struct sh2_state initial, final;
	/* first four instruction fetches from the initial PC, plus the word
	 * returned for any fetch outside them */
	uint32_t opcodes[SH2_TEST_OPCODES];
	struct sh2_cycle cyc[4];
};

/*
 * Bus model.  The vectors carry no memory contents: the reference run
 * recorded the single data access each instruction made, so the harness
 * answers that access from the vector and flags any other address.
 */
struct sh2_bus {
	uint32_t base;		/* initial PC; the opcodes live at base..base+7 */
	uint32_t opcodes[SH2_TEST_OPCODES];
	uint32_t read_addr, read_size, write_addr, write_size;
	uint64_t read_val, write_val;
	int have_read, have_write;
	int reads, writes;	/* data accesses seen */
	int addr_errors, size_errors, value_errors;
	/* most recent access, for the failure report */
	uint32_t last_addr;
	int last_size, last_is_write;
};

extern struct sh2_bus sh2_bus;

/* what the backend calls itself, printed at startup */
extern const char *sh2_core_name;

void sh2_bus_begin(const struct sh2_test *t);

void sh2_core_init(void);
void sh2_core_load(const struct sh2_state *s);
void sh2_core_get(struct sh2_state *s);
int  sh2_core_step(void);

#endif /* SH2_TEST_CORE_H */
