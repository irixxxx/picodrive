/*
 * FAME (EMU_F68K) adapter for the m68k single-step harness.
 *
 * FAME uses a flat 16 MB memory map here: all 256 fetch banks point at the
 * same buffer, which matches the 24-bit 68000 address space.
 */

#include "core.h"

#include "fame.h"

#include <stdlib.h>
#include <string.h>

static M68K_CONTEXT ctx;

/*
 * famec.c enables PICODRIVE_HACK unconditionally and therefore references the
 * second CPU context and the PicoDrive idle-detection hooks.  The standalone
 * harness does not use either; the context only exists so that FAME's
 * "is this the secondary CPU?" pointer comparison can be evaluated.
 */
M68K_CONTEXT PicoCpuFS68k;

int SekIsIdleReady(void)
{
    return 0;
}

int SekIsIdleCode(unsigned short *dst, int bytes)
{
    (void)dst;
    (void)bytes;
    return 0;
}

int SekRegisterIdlePatch(unsigned int pc, int oldop, int newop, void *ctx)
{
    (void)pc;
    (void)oldop;
    (void)newop;
    (void)ctx;
    return 0;
}

void SekFinishIdleDet(void)
{
}

static unsigned int fame_read8(unsigned int a)
{
    return mem_read8(a);
}

static unsigned int fame_read16(unsigned int a)
{
    return mem_read16(a);
}

static unsigned int fame_read32(unsigned int a)
{
    return mem_read32(a);
}

static void fame_write8(unsigned int a, unsigned char d)
{
    mem_write8(a, d);
}

static void fame_write16(unsigned int a, unsigned short d)
{
    mem_write16(a, d);
}

static void fame_write32(unsigned int a, unsigned int d)
{
    mem_write32(a, d);
}

static void fame_destroy(void *c)
{
    (void)c;
}

static void fame_reset(void *c)
{
    (void)c;
    fm68k_reset(&ctx);
}

static void fame_set_state(void *c, const struct m68k_state *s)
{
    unsigned int sr = s->sr;
    unsigned int active = (sr & 0x2000) ? s->ssp : s->usp;
    unsigned int other  = (sr & 0x2000) ? s->usp : s->ssp;
    int i;

    (void)c;

    for (i = 0; i < 8; i++)
        ctx.dreg[i].D = s->d[i];
    for (i = 0; i < 7; i++)
        ctx.areg[i].D = s->a[i];
    ctx.areg[7].D = active;
    ctx.asp = other;

    ctx.sr = (unsigned short)sr;
    ctx.pc = s->pc;

    ctx.execinfo = 0;
    ctx.interrupts[0] = 0;
    ctx.io_cycle_counter = 0;
    ctx.cycles_needed = 0;
    ctx.not_polling = 0;
}

static void fame_get_state(void *c, struct m68k_state *s)
{
    unsigned int sr = ctx.sr;
    int i;

    (void)c;

    for (i = 0; i < 8; i++)
        s->d[i] = ctx.dreg[i].D;
    for (i = 0; i < 7; i++)
        s->a[i] = ctx.areg[i].D;

    if (sr & 0x2000) {
        s->ssp = ctx.areg[7].D;
        s->usp = ctx.asp;
    } else {
        s->usp = ctx.areg[7].D;
        s->ssp = ctx.asp;
    }

    s->sr = sr;
    s->pc = ctx.pc;
    s->stopped = (ctx.execinfo & FM68K_HALTED) != 0;

    /*
     * The SST vectors report the address of the STOP instruction itself,
     * while FAME leaves PC after the immediate word.  Match the reference
     * Cyclone adapter (and the vectors) by backing up over the STOP opcode.
     */
    if (s->stopped)
        s->pc -= 4;
}

static int fame_step(void *c)
{
    (void)c;
    return fm68k_emulate(&ctx, 1, fm68k_reason_emulate);
}

static int fame_stopped(void *c)
{
    (void)c;
    return (ctx.execinfo & FM68K_HALTED) != 0;
}

struct m68k_core *fame_core_create(void)
{
    struct m68k_core *core;
    int i;

    memset(&ctx, 0, sizeof(ctx));

    ctx.read_byte = fame_read8;
    ctx.read_word = fame_read16;
    ctx.read_long = fame_read32;
    ctx.write_byte = fame_write8;
    ctx.write_word = fame_write16;
    ctx.write_long = fame_write32;
    ctx.reset_handler = NULL;
    ctx.iack_handler = NULL;

    for (i = 0; i < M68K_FETCHBANK1; i++)
        ctx.Fetch[i] = (uintptr_t)memory;

    fm68k_init();
    fm68k_reset(&ctx);

    core = calloc(1, sizeof(*core));
    if (!core)
        return NULL;

    core->name = "FAME";
    core->ctx = &ctx;
    core->destroy = fame_destroy;
    core->reset = fame_reset;
    core->set_state = fame_set_state;
    core->get_state = fame_get_state;
    core->step = fame_step;
    core->stopped = fame_stopped;
    return core;
}
