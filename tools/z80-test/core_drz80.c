/*
 * DrZ80 (EMU_DRZ80) adapter for the z80 single-step harness.
 *
 * DrZ80 is PicoDrive's 32-bit ARM assembly core, so this adapter and the
 * vectors are built for arm and run under qemu-arm (or on a handheld).
 *
 * The core is compiled as PicoDrive ships it: xmap mode, which fetches opcodes
 * through a direct pointer and routes the rest through page tables of
 * "host pointer >> 1".  The harness has a flat 64K, so every one of the 64
 * pages points at the start of the same buffer.
 */

#include "core.h"

#include <stdlib.h>

#include "drz80.h"

static struct DrZ80 drz80;

/* xmap entries hold (page base - page start) >> 1; the z80 address is added
 * whole, so a flat 64K map is the buffer base for every page. */
static uintptr_t drz80_read_map[0x10000 >> 10];
static uintptr_t drz80_write_map[0x10000 >> 10];

/* DrZ80 keeps F in its own layout and exports the conversion tables. */
extern unsigned char DrZ80_ARM[];
extern unsigned char DrARM_Z80[];

static uint8_t drz80_in(uint16_t port)
{
    return z80_port_in(port);
}

static void drz80_out(uint16_t port, uint8_t value)
{
    z80_port_out(port, value);
}

static void drz80_destroy(void *ctx)
{
    (void)ctx;
}

/* Every compared register is written by set_state, so reset has nothing to do. */
static void drz80_reset(void *ctx)
{
    (void)ctx;
}

static void drz80_set_state(void *ctx, const struct z80_state *s)
{
    (void)ctx;

    /* A, I and the register pairs live in the top byte/halfword of their word. */
    drz80.Z80A  = (uint32_t)(s->af >> 8) << 24;
    drz80.Z80F  = DrZ80_ARM[s->af & 0xff];
    drz80.Z80BC = (uint32_t)s->bc << 16;
    drz80.Z80DE = (uint32_t)s->de << 16;
    drz80.Z80HL = (uint32_t)s->hl << 16;
    drz80.Z80IX = (uint32_t)s->ix << 16;
    drz80.Z80IY = (uint32_t)s->iy << 16;
    drz80.Z80I  = (uint32_t)s->i << 24;

    drz80.Z80A2  = (uint32_t)(s->af2 >> 8) << 24;
    drz80.Z80F2  = DrZ80_ARM[s->af2 & 0xff];
    drz80.Z80BC2 = (uint32_t)s->bc2 << 16;
    drz80.Z80DE2 = (uint32_t)s->de2 << 16;
    drz80.Z80HL2 = (uint32_t)s->hl2 << 16;

    /* Z80R holds (count << 1) | bit7, not the architectural byte */
    drz80.Z80R = (uint8_t)((s->r << 1) | (s->r >> 7));

    drz80.Z80SP = s->sp;
    drz80.Z80PC_BASE = (uint32_t)(uintptr_t)memory;
    drz80.Z80PC = drz80.Z80PC_BASE + s->pc;

    drz80.Z80IF = (uint8_t)((s->iff1 ? 1 : 0) | (s->iff2 ? 2 : 0));
    drz80.Z80IM = s->im;
    drz80.Z80_IRQ = 0;
    drz80.z80irqvector = 0;
}

static void drz80_get_state(void *ctx, struct z80_state *s)
{
    (void)ctx;

    memset(s, 0, sizeof(*s));

    s->af  = (uint16_t)(((drz80.Z80A >> 24) & 0xff) << 8);
    s->af |= DrARM_Z80[drz80.Z80F];
    s->bc  = (uint16_t)(drz80.Z80BC >> 16);
    s->de  = (uint16_t)(drz80.Z80DE >> 16);
    s->hl  = (uint16_t)(drz80.Z80HL >> 16);
    s->ix  = (uint16_t)(drz80.Z80IX >> 16);
    s->iy  = (uint16_t)(drz80.Z80IY >> 16);

    s->af2  = (uint16_t)(((drz80.Z80A2 >> 24) & 0xff) << 8);
    s->af2 |= DrARM_Z80[drz80.Z80F2];
    s->bc2 = (uint16_t)(drz80.Z80BC2 >> 16);
    s->de2 = (uint16_t)(drz80.Z80DE2 >> 16);
    s->hl2 = (uint16_t)(drz80.Z80HL2 >> 16);

    s->i = (uint8_t)(drz80.Z80I >> 24);
    s->r = (uint8_t)((drz80.Z80R >> 1) | (drz80.Z80R << 7));

    s->sp = (uint16_t)drz80.Z80SP;
    s->pc = (uint16_t)(drz80.Z80PC - drz80.Z80PC_BASE);

    s->im   = drz80.Z80IM;
    s->iff1 = !!(drz80.Z80IF & 1);
    s->iff2 = !!(drz80.Z80IF & 2);
}

/* DrZ80Run() returns the cycles left over; a budget of one stops after the
 * current instruction, prefixed opcodes included. */
static int drz80_step(void *ctx)
{
    (void)ctx;

    return 1 - DrZ80Run(&drz80, 1);
}

struct z80_core *drz80_core_create(void)
{
    struct z80_core *core;
    uintptr_t base = ((uintptr_t)memory) >> 1;
    unsigned i;

    memset(&drz80, 0, sizeof(drz80));

    for (i = 0; i < sizeof(drz80_read_map) / sizeof(drz80_read_map[0]); i++) {
        drz80_read_map[i] = base;
        drz80_write_map[i] = base;
    }

    drz80.z80_read8   = (void *)drz80_read_map;
    drz80.z80_write8  = (void *)drz80_write_map;
    drz80.z80_read16  = NULL;
    drz80.z80_write16 = NULL;
    drz80.z80_in      = drz80_in;
    drz80.z80_out     = drz80_out;
    drz80.z80_rebasePC = NULL;
    drz80.z80_rebaseSP = NULL;
    drz80.z80_irq_callback = NULL;

    core = calloc(1, sizeof(*core));
    if (!core)
        return NULL;

    core->name = "DrZ80";
    core->ctx = &drz80;
    core->destroy = drz80_destroy;
    core->reset = drz80_reset;
    core->set_state = drz80_set_state;
    core->get_state = drz80_get_state;
    core->step = drz80_step;
    return core;
}
