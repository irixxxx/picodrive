/*
 * Adapter: runs the MAME SH-2 interpreter (cpu/sh2/mame/sh2pico.c) on the
 * single-step harness bus.
 *
 * sh2pico.c calls the p32x_sh2 read and write handlers, which in PicoDrive
 * are the 32X bus handlers.  Here they are answered from the vector's single
 * recorded data access, and instruction fetches come from its opcode list.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "sh2.h"	/* cpu/sh2/sh2.h */

const char *sh2_core_name = "MAME SH-2 interpreter (cpu/sh2/mame)";
static SH2 cpu;

/* set before each step: the first 16-bit read of a step is an instruction
 * fetch, and so is the delay slot the core runs right after a branch */
static int expect_fetch;

/* The core logs illegal opcodes through lprintf; a test that reaches one is
 * reported by its state, not by the log. */
void lprintf(const char *fmt, ...)
{
	(void)fmt;
}

/* The interpreter only calls this when test_irq is set, which the harness
 * never does. */
void sh2_do_irq(SH2 *sh2, int level, int vector)
{
	(void)sh2;
	(void)level;
	(void)vector;
}

void sh2_core_init(void)
{
	memset(&cpu, 0, sizeof(cpu));
}

void sh2_core_load(const struct sh2_state *s)
{
	memset(&cpu, 0, sizeof(cpu));
	memcpy(cpu.r, s->r, sizeof(cpu.r));
	cpu.pc = cpu.ppc = s->pc;
	cpu.pr = s->pr;
	cpu.sr = s->sr;
	cpu.gbr = s->gbr;
	cpu.vbr = s->vbr;
	cpu.mach = s->mach;
	cpu.macl = s->macl;
	/* delay, test_irq, pending_level and icount stay zero */
}

void sh2_core_get(struct sh2_state *s)
{
	memset(s, 0, sizeof(*s));
	memcpy(s->r, cpu.r, sizeof(s->r));
	s->pc = cpu.pc;
	s->pr = cpu.pr;
	s->sr = cpu.sr;
	s->gbr = cpu.gbr;
	s->vbr = cpu.vbr;
	s->mach = cpu.mach;
	s->macl = cpu.macl;
}

/*
 * Run one instruction.  A delayed branch executes its delay slot inside the
 * same call, so that slot is a second instruction: ppc then points at the
 * slot address instead of the address the call started from.
 */
int sh2_core_step(void)
{
	uint32_t before = cpu.pc;

	expect_fetch = 1;
	sh2_execute_interpreter(&cpu, 1);

	return 1 + (cpu.ppc != before);
}

/* first word of the instruction stream at addr, or the "outside" word */
static uint32_t window_word(uint32_t addr)
{
	int32_t d = (int32_t)(addr - sh2_bus.base);

	if (d >= 0 && d < 8)
		return sh2_bus.opcodes[d >> 1];
	return sh2_bus.opcodes[SH2_TEST_OPCODES - 1];
}

static uint32_t mask_for(int size)
{
	return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffffu;
}


static uint32_t data_read(uint32_t addr, int size)
{
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
	return (uint32_t)sh2_bus.read_val & mask_for(size);
}

static void data_write(uint32_t addr, uint32_t val, int size)
{
	uint32_t mask = mask_for(size);

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
	if ((val & mask) != ((uint32_t)sh2_bus.write_val & mask))
		sh2_bus.value_errors++;
}

static uint32_t bus_read(uint32_t addr, int size)
{
	/* the core fetches the instruction at the top of each step and the
	 * delay slot right after a branch.  Those are the opcode stream,
	 * while a data read of the same address is the vector's recorded
	 * value (a PC-relative load can point into the instruction stream) */
	if (size == 2 && (expect_fetch || cpu.delay)) {
		expect_fetch = 0;
		return window_word(addr);
	}

	/* the core also peeks the instruction it is executing to spot DT/BRA
	 * loops (`RW(sh2, ppc & AM)`); the value is only compared, so answer
	 * it from the opcode stream instead of flagging a data access */
	if (size == 2 && addr == (cpu.ppc & 0xc7ffffffu) &&
	    addr != sh2_bus.read_addr)
		return window_word(addr);

	return data_read(addr, size);
}

u32 REGPARM(2) p32x_sh2_read8(u32 a, SH2 *sh2)
{
	(void)sh2;
	return bus_read(a, 1);
}

u32 REGPARM(2) p32x_sh2_read16(u32 a, SH2 *sh2)
{
	(void)sh2;
	return bus_read(a, 2);
}

u32 REGPARM(2) p32x_sh2_read32(u32 a, SH2 *sh2)
{
	(void)sh2;
	return bus_read(a, 4);
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

