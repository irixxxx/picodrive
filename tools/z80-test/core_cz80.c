/*
 * CZ80 (EMU_CZ80) adapter for the z80 single-step harness.
 *
 * CZ80 is PicoDrive's portable C z80 core.  The harness builds a copy of the
 * core with PICODRIVE_HACKS turned off (see the Makefile) so that memory goes
 * through the plain Read_Byte/Write_Byte callbacks instead of PicoDrive's
 * banked z80 map.
 */

#include "core.h"

#include "cz80.h"

static cz80_struc cpu;

static uint8_t cz80_read(uint32_t address)
{
    return mem_read8(address);
}

static void cz80_write(uint32_t address, uint8_t data)
{
    mem_write8(address, data);
}

static uint8_t cz80_in(uint16_t port)
{
    return z80_port_in(port);
}

static void cz80_out(uint16_t port, uint8_t value)
{
    z80_port_out(port, value);
}

static int32_t cz80_irq(int32_t irqline)
{
    (void)irqline;
    return 0xff;
}

static void cz80_destroy(void *ctx)
{
    (void)ctx;
}

/* Every compared register is written by set_state, so reset has nothing to do. */
static void cz80_reset(void *ctx)
{
    (void)ctx;
}

static void cz80_set_state(void *ctx, const struct z80_state *s)
{
    (void)ctx;

    cpu.Status = 0;
    cpu.ICount = 0;
    cpu.ExtraCycles = 0;
    cpu.IRQLine = 0;
    cpu.IRQState = 0;

    Cz80_Set_Reg(&cpu, CZ80_AF, s->af);
    Cz80_Set_Reg(&cpu, CZ80_BC, s->bc);
    Cz80_Set_Reg(&cpu, CZ80_DE, s->de);
    Cz80_Set_Reg(&cpu, CZ80_HL, s->hl);
    Cz80_Set_Reg(&cpu, CZ80_IX, s->ix);
    Cz80_Set_Reg(&cpu, CZ80_IY, s->iy);
    Cz80_Set_Reg(&cpu, CZ80_SP, s->sp);
    Cz80_Set_Reg(&cpu, CZ80_AF2, s->af2);
    Cz80_Set_Reg(&cpu, CZ80_BC2, s->bc2);
    Cz80_Set_Reg(&cpu, CZ80_DE2, s->de2);
    Cz80_Set_Reg(&cpu, CZ80_HL2, s->hl2);
    Cz80_Set_Reg(&cpu, CZ80_R, s->r);
    Cz80_Set_Reg(&cpu, CZ80_I, s->i);
    Cz80_Set_Reg(&cpu, CZ80_IM, s->im);
    Cz80_Set_Reg(&cpu, CZ80_IFF1, s->iff1);
    Cz80_Set_Reg(&cpu, CZ80_IFF2, s->iff2);
    Cz80_Set_Reg(&cpu, CZ80_PC, s->pc);
}

static void cz80_get_state(void *ctx, struct z80_state *s)
{
    (void)ctx;

    memset(s, 0, sizeof(*s));
    s->af = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_AF);
    s->bc = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_BC);
    s->de = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_DE);
    s->hl = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_HL);
    s->ix = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_IX);
    s->iy = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_IY);
    s->sp = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_SP);
    s->pc = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_PC);
    s->af2 = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_AF2);
    s->bc2 = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_BC2);
    s->de2 = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_DE2);
    s->hl2 = (uint16_t)Cz80_Get_Reg(&cpu, CZ80_HL2);
    s->i = (uint8_t)Cz80_Get_Reg(&cpu, CZ80_I);
    s->r = (uint8_t)Cz80_Get_Reg(&cpu, CZ80_R);
    s->im = (uint8_t)Cz80_Get_Reg(&cpu, CZ80_IM);
    s->iff1 = (uint8_t)(Cz80_Get_Reg(&cpu, CZ80_IFF1) ? 1 : 0);
    s->iff2 = (uint8_t)(Cz80_Get_Reg(&cpu, CZ80_IFF2) ? 1 : 0);
}

/*
 * Cz80_Exec() runs until the cycle budget is used up, so a budget of one
 * executes exactly one instruction and returns the T-states it took.
 */
static int cz80_step(void *ctx)
{
    (void)ctx;

    return (int)Cz80_Exec(&cpu, 1);
}

struct z80_core *cz80_core_create(void)
{
    struct z80_core *core;

    Cz80_Init(&cpu);
    Cz80_Set_ReadB(&cpu, cz80_read);
    Cz80_Set_WriteB(&cpu, cz80_write);
    Cz80_Set_INPort(&cpu, cz80_in);
    Cz80_Set_OUTPort(&cpu, cz80_out);
    Cz80_Set_IRQ_Callback(&cpu, cz80_irq);
    Cz80_Set_Fetch(&cpu, 0x0000, 0xffff, (FPTR)memory);

    core = calloc(1, sizeof(*core));
    if (!core)
        return NULL;

    core->name = "CZ80";
    core->ctx = &cpu;
    core->destroy = cz80_destroy;
    core->reset = cz80_reset;
    core->set_state = cz80_set_state;
    core->get_state = cz80_get_state;
    core->step = cz80_step;
    return core;
}
