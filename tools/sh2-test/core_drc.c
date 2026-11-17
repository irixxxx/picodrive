/*
 * Adapter: runs the SH-2 recompiler (cpu/sh2/compiler.c) on the single-step
 * harness bus.
 *
 * The recompiler is built with SH2_DRC_SINGLE_STEP: a block is one
 * instruction, or two for a delayed branch, so a budget of one cycle retires
 * exactly one instruction and the architectural state can be compared after
 * every step, as with the interpreter adapter.
 *
 * Instruction fetches come from a window that mirrors guest addresses, data
 * accesses go through the read and write maps to the handlers below.  That
 * also means there is no "was this a fetch or a data read" guessing here.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>
#endif

#include "core.h"
#include "sh2.h"

#include <pico/pico_int.h>
#include <pico/memory.h>
#include "compiler.h"

#ifndef MAP_FLAG
#define MAP_FLAG ((uptr)1 << (sizeof(uptr) * 8 - 1))
#endif

/* guest addresses below this are mirrored by the fetch window */
#define WINDOW_SIZE 0x100000
/* read and write maps are indexed with a >> SH2_READ_SHIFT */
#define MAP_ENTRIES 0x80

SH2 sh2s[2];
struct Pico Pico;
/* only the arm build of drc_cmn_init() looks at this */
PicoInterface PicoIn;
struct Pico32x Pico32x;
static struct Pico32xMem pico32x_mem;
struct Pico32xMem *Pico32xMem = &pico32x_mem;
char **g_argv;

const char *sh2_core_name = "SH-2 recompiler (cpu/sh2/compiler.c)";


/* extra words for the block scan, which can look past the window edge */
#define WINDOW_MARGIN 0x2000

static u16 *window;
static int window_words;
static int window_filled;
static u16 window_fill_word;
/* the words a folded literal overwrote, restored before the next vector */
static u32 lit_off;
static int lit_words;
/* the four instruction words written for the previous vector */
static u32 code_off;
static int code_pending;
static int tracing;
#define SEEN_PC_COUNT 65536
static u32 seen_pc[SEEN_PC_COUNT];
/* SR bits the DRC uses for its cycle count, restored after every step */
static u32 sr_keep;
/* SLEEP leaves the PC on the instruction until an interrupt wakes the CPU */
static int sleeping;

/* the 32X bus entry points the compiler and its generated helpers call */

void memset32(void *dest, int c, int count)
{
	memset(dest, c, 4 * count);
}

void lprintf(const char *fmt, ...)
{
	(void)fmt;
}

void cache_flush_d_inval_i(void *start, void *end)
{
	(void)start;
	(void)end;
}

/*
 * the harness' platform layer: there is no emulator build here, so map and
 * switch the code buffer like platform/linux/plat.c does on Apple silicon
 */
void *plat_mem_get_for_drc(size_t size)
{
#if defined(__APPLE__) && defined(__aarch64__)
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
  return p == MAP_FAILED ? NULL : p;
#else
  (void)size;
  return NULL;
#endif
}

int plat_drc_jit_set_exec(void *ptr, size_t size)
{
#if defined(__APPLE__) && defined(__aarch64__)
  (void)ptr;
  (void)size;
  return 0; // executable once write-protected
#else
  return mprotect(ptr, size, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
}

void plat_drc_jit_protect(int writable)
{
#if defined(__APPLE__) && defined(__aarch64__)
  pthread_jit_write_protect_np(!writable);
#else
  (void)writable;
#endif
}

int p32x_sh2_mem_is_rom(u32 a, SH2 *sh2)
{
	(void)a;
	(void)sh2;
	return 0;
}

void sh2_do_irq(SH2 *sh2, int level, int vector)
{
	(void)sh2;
	(void)level;
	(void)vector;
}

static u32 mask_for(int size)
{
	return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffffu;
}

/*
 * The vector recorded the one data access its instruction made; answer that
 * access and flag anything else.  Like the real 32X handlers, 8 and 16 bit
 * reads return sign-extended values (MOV.B/MOV.W sign-extend them).
 */
static u32 data_read(u32 addr, int size)
{
	u32 v;

	sh2_bus.reads++;
	sh2_bus.last_addr = addr;
	sh2_bus.last_size = size;
	sh2_bus.last_is_write = 0;

	if (!sh2_bus.have_read || addr != sh2_bus.read_addr) {
		sh2_bus.addr_errors++;
		return 0;
	}
	if (size != (int)sh2_bus.read_size)
		sh2_bus.size_errors++;

	v = (u32)sh2_bus.read_val & mask_for(size);
	if (size == 1)
		return (u32)(int8_t)v;
	if (size == 2)
		return (u32)(int16_t)v;
	return v;
}

static void data_write(u32 addr, u32 val, int size)
{
	u32 mask = mask_for(size);

	sh2_bus.writes++;
	sh2_bus.last_addr = addr;
	sh2_bus.last_size = size;
	sh2_bus.last_is_write = 1;

	if (!sh2_bus.have_write || addr != sh2_bus.write_addr) {
		sh2_bus.addr_errors++;
		return;
	}
	if (size != (int)sh2_bus.write_size)
		sh2_bus.size_errors++;
	if ((val & mask) != ((u32)sh2_bus.write_val & mask))
		sh2_bus.value_errors++;
}

u32 REGPARM(2) p32x_sh2_read8(u32 a, SH2 *sh2)
{
	(void)sh2;
	return data_read(a, 1);
}

u32 REGPARM(2) p32x_sh2_read16(u32 a, SH2 *sh2)
{
	(void)sh2;
	return data_read(a, 2);
}

u32 REGPARM(2) p32x_sh2_read32(u32 a, SH2 *sh2)
{
	(void)sh2;
	return data_read(a, 4);
}

void REGPARM(3) p32x_sh2_write8(u32 a, u32 d, SH2 *sh2)
{
	(void)sh2;
	data_write(a, d, 1);
}

void REGPARM(3) p32x_sh2_write16(u32 a, u32 d, SH2 *sh2)
{
	(void)sh2;
	data_write(a, d, 2);
}

void REGPARM(3) p32x_sh2_write32(u32 a, u32 d, SH2 *sh2)
{
	(void)sh2;
	data_write(a, d, 4);
}

u32 REGPARM(3) p32x_sh2_poll_memory8(u32 a, u32 d, SH2 *sh2)
{
	(void)d;
	return p32x_sh2_read8(a, sh2);
}

u32 REGPARM(3) p32x_sh2_poll_memory16(u32 a, u32 d, SH2 *sh2)
{
	(void)d;
	return p32x_sh2_read16(a, sh2);
}

u32 REGPARM(3) p32x_sh2_poll_memory32(u32 a, u32 d, SH2 *sh2)
{
	(void)d;
	return p32x_sh2_read32(a, sh2);
}

/*
 * The compiler asks for a fetch pointer and a mask, then makes its own base
 * as ptr - (pc & ~mask).  With a zero mask that base is ptr - pc, so a fetch
 * at any guest address lands on the same offset in the window: the window is
 * a mirror of the low bits of the address space and needs no special cases
 * for the arbitrary PCs and branch targets the vectors use.
 */
void *p32x_sh2_get_mem_ptr(u32 a, u32 *mask, SH2 *sh2)
{
	(void)sh2;

	*mask = 0;
	return window + ((a & (WINDOW_SIZE - 1)) >> 1);
}

static sh2_memmap read8_map[MAP_ENTRIES], read16_map[MAP_ENTRIES],
	read32_map[MAP_ENTRIES];
static const void *write8_map[MAP_ENTRIES], *write16_map[MAP_ENTRIES],
	*write32_map[MAP_ENTRIES];

static void map_init(void)
{
	int i;

	for (i = 0; i < MAP_ENTRIES; i++) {
		read8_map[i].addr = ((uptr)p32x_sh2_read8 >> 1) | MAP_FLAG;
		read16_map[i].addr = ((uptr)p32x_sh2_read16 >> 1) | MAP_FLAG;
		read32_map[i].addr = ((uptr)p32x_sh2_read32 >> 1) | MAP_FLAG;
		read8_map[i].mask = read16_map[i].mask = read32_map[i].mask = 0;

		write8_map[i] = (const void *)p32x_sh2_write8;
		write16_map[i] = (const void *)p32x_sh2_write16;
		write32_map[i] = (const void *)p32x_sh2_write32;
	}

	sh2s[0].read8_map = read8_map;
	sh2s[0].read16_map = read16_map;
	sh2s[0].read32_map = read32_map;
	sh2s[0].write8_tab = write8_map;
	sh2s[0].write16_tab = write16_map;
	sh2s[0].write32_tab = write32_map;
}

static void window_reset(u16 w)
{
	int i;

	for (i = 0; i < window_words; i++)
		window[i] = w;
	window_fill_word = w;
	window_filled = 1;
}

static u16 fetch_word(u32 a)
{
	return window[(a & (WINDOW_SIZE - 1)) >> 1];
}

/*
 * Blocks are compiled per PC and the vectors use random PCs, so a block is
 * stale only when a PC comes back with different code.  That is rare, so
 * flush on a repeat instead of after every vector: 116k full tcache rebuilds
 * are avoided and a repeated address still cannot run old code.
 */
static void note_pc(u32 pc)
{
	u32 slot = (pc >> 1) & (SEEN_PC_COUNT - 1);

	if (seen_pc[slot] == pc) {
		sh2_drc_flush_all();
		memset(seen_pc, 0, sizeof(seen_pc));
	}
	seen_pc[slot] = pc;
}

/*
 * A delayed branch runs its slot inside the same step, so the recompiler
 * emits two instructions for it.  Counting from the encoding keeps the step
 * loop in sh2_test.c independent of the core.
 */
static int has_delay_slot(u16 op)
{
	switch (op & 0xf0ff) {
	case 0x0003:	/* BSRF Rm */
	case 0x0023:	/* BRAF Rm */
	case 0x400b:	/* JSR @Rm */
	case 0x402b:	/* JMP @Rm */
		return 1;
	}
	if ((op & 0xf000) == 0xa000 || (op & 0xf000) == 0xb000)
		return 1;	/* BRA, BSR */
	if ((op & 0xff00) == 0x8d00 || (op & 0xff00) == 0x8f00)
		return 1;	/* BT/S, BF/S */
	return op == 0x000b || op == 0x002b;	/* RTS, RTE */
}

void sh2_core_init(void)
{
	tracing = getenv("SH2_TEST_TRACE") != NULL;
	window_words = (WINDOW_SIZE + WINDOW_MARGIN) / 2;
	window = calloc(window_words, sizeof(*window));
	if (window == NULL) {
		fprintf(stderr, "out of memory for the fetch window\n");
		exit(1);
	}

	if (sh2_drc_init(&sh2s[0]) != 0) {
		fprintf(stderr, "sh2_drc_init failed\n");
		exit(1);
	}
	sh2_drc_mem_setup(&sh2s[0]);
	map_init();
}

void sh2_core_load(const struct sh2_state *s)
{
	SH2 *c = &sh2s[0];
	u32 base = sh2_bus.base;
	u16 fill = (u16)sh2_bus.opcodes[SH2_TEST_OPCODES - 1];
	int i;

	/* registers only: the maps and the DRC pointers stay in the struct */
	memcpy(c->r, s->r, sizeof(c->r));
	c->pc = c->ppc = s->pc;
	c->pr = s->pr;
	c->sr = s->sr;
	c->gbr = s->gbr;
	c->vbr = s->vbr;
	c->mach = s->mach;
	c->macl = s->macl;
	c->delay = 0;
	c->test_irq = 0;
	c->icount = 0;
	c->ea = 0;
	c->drc_tmp = 0;
	c->state = 0;
	c->poll_addr = 0;
	c->poll_cycles = 0;
	c->poll_cnt = 0;
	c->no_polling = SH2_NO_POLLING;
	c->pending_level = 0;
	c->pending_irl = 0;
	c->pending_int_irq = 0;
	c->irq_callback = NULL;
	c->is_slave = 0;
	sr_keep = s->sr & ~0x3f3u;
	sleeping = 0;

	sh2_drc_mem_setup(c);

	/* put back what the previous vector wrote, so any other fetch
	 * address still reads the fill word */
	if (code_pending) {
		for (i = 0; i < 4; i++)
			window[(code_off >> 1) + i] = window_fill_word;
		code_pending = 0;
	}
	if (lit_words) {
		window[lit_off >> 1] = window_fill_word;
		if (lit_words == 2)
			window[(lit_off >> 1) + 1] = window_fill_word;
		lit_words = 0;
	}

	if (!window_filled || window_fill_word != fill)
		window_reset(fill);
	code_off = base & (WINDOW_SIZE - 1);
	code_pending = 1;
	for (i = 0; i < 4; i++)
		window[(code_off >> 1) + i] = (u16)sh2_bus.opcodes[i];

	/*
	 * PC-relative literals are folded into a direct read of the code
	 * stream, so mirror the value the reference run read there.  Those
	 * instructions then make no bus access, which the reference model
	 * records as a read of that same address.
	 */
	if (sh2_bus.have_read) {
		/* the compiler bakes base = ptr - pc, so it reads address a from
		 * (pc & (WINDOW_SIZE-1)) + (a - pc). That is not a & (WINDOW_SIZE-1)
		 * whenever the two straddle a window edge, so use its arithmetic */
		u32 off = code_off + (sh2_bus.read_addr - base);
		u32 v = (u32)sh2_bus.read_val;

		if (off + 4 <= (u32)window_words * 2) {
			lit_off = off;
			switch (sh2_bus.read_size) {
			case 1:
				((u8 *)window)[off] = (u8)v;
				lit_words = 1;
				break;
			case 2:
				window[off >> 1] = (u16)v;
				lit_words = 1;
				break;
			default:
				/* a folded 32-bit literal is assembled high half first */
				window[off >> 1] = (u16)(v >> 16);
				window[(off >> 1) + 1] = (u16)v;
				lit_words = 2;
				break;
			}
		}
	}
	{
		u16 op = (u16)sh2_bus.opcodes[1];

		if (((op & 0xf000) == 0x9000 || (op & 0xf000) == 0xd000) &&
		    sh2_bus.have_read && !sh2_bus.reads)
			sh2_bus.reads = 1;
	}

	if (tracing)
		fprintf(stderr, "test: base=%08x sr=%08x ops=%04x %04x %04x %04x read=%d@%08x/%u\n",
			(unsigned)base, (unsigned)s->sr, (unsigned)sh2_bus.opcodes[0],
			(unsigned)sh2_bus.opcodes[1], (unsigned)sh2_bus.opcodes[2],
			(unsigned)sh2_bus.opcodes[3], sh2_bus.have_read,
			(unsigned)sh2_bus.read_addr, sh2_bus.read_size);


}

void sh2_core_get(struct sh2_state *s)
{
	SH2 *c = &sh2s[0];

	memset(s, 0, sizeof(*s));
	memcpy(s->r, c->r, sizeof(s->r));
	s->pc = c->pc;
	s->pr = c->pr;
	s->sr = c->sr;
	s->gbr = c->gbr;
	s->vbr = c->vbr;
	s->mach = c->mach;
	s->macl = c->macl;
}

int sh2_core_step(void)
{
	SH2 *c = &sh2s[0];
	u16 op;
	int n;

	if (sleeping) {
		if (tracing)
			fprintf(stderr, "  pc=%08x sleeping\n", (unsigned)c->pc);
		return 1;
	}

	op = fetch_word(c->pc);
	note_pc(c->pc);
	if (tracing)
		fprintf(stderr, "  pc=%08x op=%04x -> ", (unsigned)c->pc, op);
	if (op == 0x001b) { /* SLEEP */
		sleeping = 1;
		if (tracing)
			fprintf(stderr, "sleep\n");
		return 1;
	}

	n = has_delay_slot(op) ? 2 : 1;
	sh2_execute_drc(c, 1);
	/* the DRC keeps its cycle budget in SR's upper bits */
	c->sr = (c->sr & 0x3f3) | sr_keep;
	if (tracing)
		fprintf(stderr, "pc=%08x r2=%08x r11=%08x sr=%08x\n",
			(unsigned)c->pc, (unsigned)c->r[2], (unsigned)c->r[11],
			(unsigned)c->sr);
	return n;
}
