/*
 * Cyclone (EMU_C68K) adapter for the m68k single-step harness.
 *
 * Cyclone is ARM assembly and its struct Cyclone.membase is an int, so this
 * adapter (and the whole binary) must be built for 32-bit ARM.  Build it with
 * the arm-linux-gnueabihf toolchain and run it under qemu-arm.
 */

#include "core.h"

#include "Cyclone.h"

#include <stdlib.h>
#include <string.h>

static struct Cyclone cpu;

/*
 * These handlers are deliberately non-static: if Cyclone is ever built with
 * MEMHANDLERS_DIRECT_PREFIX, the generated assembly calls them by name.
 */
unsigned int cyclone_checkpc(unsigned int pc)
{
    unsigned int pc_addr;

    pc -= (unsigned int)cpu.membase;
    pc_addr = (unsigned int)(uintptr_t)&memory[pc & (MEM_SIZE - 2)];
    cpu.membase = (int)(pc_addr - pc);
    return pc_addr;
}

unsigned int cyclone_read8(unsigned int a)
{
    return mem_read8(a);
}

unsigned int cyclone_read16(unsigned int a)
{
    return mem_read16(a);
}

unsigned int cyclone_read32(unsigned int a)
{
    return mem_read32(a);
}

void cyclone_write8(unsigned int a, unsigned char d)
{
    mem_write8(a, d);
}

void cyclone_write16(unsigned int a, unsigned short d)
{
    mem_write16(a, d);
}

void cyclone_write32(unsigned int a, unsigned int d)
{
    mem_write32(a, d);
}

static void cyclone_destroy(void *ctx)
{
    (void)ctx;
}

static void cyclone_reset(void *ctx)
{
    (void)ctx;
    CycloneReset(&cpu);
}

static void cyclone_set_state(void *ctx, const struct m68k_state *s)
{
    unsigned int sr = s->sr;
    unsigned int active = (sr & 0x2000) ? s->ssp : s->usp;
    unsigned int other  = (sr & 0x2000) ? s->usp : s->ssp;
    int i;

    (void)ctx;

    for (i = 0; i < 8; i++)
        cpu.d[i] = s->d[i];
    for (i = 0; i < 7; i++)
        cpu.a[i] = s->a[i];
    cpu.a[7] = active;
    cpu.osp = other;

    CycloneSetSr(&cpu, sr);

    cpu.state_flags = 0;
    cpu.cycles = 0;
    cpu.irq = 0;
    cpu.not_pol = 0;
    cpu.prev_pc = 0;

    cpu.membase = 0;
    cpu.pc = s->pc;
    cpu.pc = cyclone_checkpc(cpu.pc);
}

static void cyclone_get_state(void *ctx, struct m68k_state *s)
{
    unsigned int sr = CycloneGetSr(&cpu);
    int i;

    (void)ctx;

    for (i = 0; i < 8; i++)
        s->d[i] = cpu.d[i];
    for (i = 0; i < 7; i++)
        s->a[i] = cpu.a[i];

    if (sr & 0x2000) {
        s->ssp = cpu.a[7];
        s->usp = cpu.osp;
    } else {
        s->usp = cpu.a[7];
        s->ssp = cpu.osp;
    }

    s->sr = sr;
    s->pc = cpu.pc - (unsigned int)cpu.membase;
    s->stopped = (cpu.state_flags & 1) != 0;
    if (s->stopped)
        s->pc -= 4;
}

static int cyclone_step(void *ctx)
{
    (void)ctx;

    CycloneRun(&cpu);
    return -cpu.cycles;
}

static int cyclone_stopped(void *ctx)
{
    (void)ctx;
    return (cpu.state_flags & 1) != 0;
}

struct m68k_core *cyclone_core_create(void)
{
    struct m68k_core *core;

    memset(&cpu, 0, sizeof(cpu));

    cpu.checkpc = cyclone_checkpc;
    cpu.read8 = cyclone_read8;
    cpu.read16 = cyclone_read16;
    cpu.read32 = cyclone_read32;
    cpu.write8 = cyclone_write8;
    cpu.write16 = cyclone_write16;
    cpu.write32 = cyclone_write32;
    cpu.fetch8 = cyclone_read8;
    cpu.fetch16 = cyclone_read16;
    cpu.fetch32 = cyclone_read32;
    cpu.IrqCallback = NULL;
    cpu.ResetCallback = NULL;
    cpu.UnrecognizedCallback = NULL;

    CycloneInit();
    CycloneReset(&cpu);

    core = calloc(1, sizeof(*core));
    if (!core)
        return NULL;

    core->name = "Cyclone";
    core->ctx = &cpu;
    core->destroy = cyclone_destroy;
    core->reset = cyclone_reset;
    core->set_state = cyclone_set_state;
    core->get_state = cyclone_get_state;
    core->step = cyclone_step;
    core->stopped = cyclone_stopped;
    return core;
}
