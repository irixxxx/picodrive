/*
 * Musashi (EMU_M68K) adapter for the m68k single-step harness.
 *
 * Musashi uses callback functions for all memory accesses.  The CPU context is
 * a normal m68ki_cpu_core, and m68k_set_context() selects it for the public
 * m68k_get_reg()/m68k_set_reg() API.
 */

#include "core.h"

#include "m68kcpu.h"

#include <stdlib.h>
#include <string.h>

static m68ki_cpu_core cpu;

/* ---- memory callbacks required by Musashi ------------------------------ */

unsigned int m68k_read_memory_8(unsigned int address)
{
    return mem_read8(address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return mem_read16(address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return mem_read32(address);
}

/*
 * m68kcpu.h maps m68k_read_immediate_* and m68k_read_pcrelative_* to its
 * internal m68ki_read_program_* helpers when M68K_SEPARATE_READS is off, so
 * those public symbols are not needed (and defining them would collide with
 * the macros).
 */

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return mem_read8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return mem_read16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return mem_read32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    mem_write8(address, (uint8_t)value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    mem_write16(address, (uint16_t)value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    mem_write32(address, value);
}

void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
    /* Undocumented 68k predecrement long write order: high word first. */
    mem_write16(address + 2, (uint16_t)(value >> 16));
    mem_write16(address, (uint16_t)value);
}

/* ---- optional callbacks ------------------------------------------------ */

static int musashi_int_ack(int level)
{
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}

static int musashi_tas(void)
{
    /* Nonzero: allow the TAS write-back. */
    return 1;
}

/* ---- core adapter ------------------------------------------------------ */

static void musashi_destroy(void *c)
{
    (void)c;
}

static void musashi_reset(void *c)
{
    (void)c;
    m68k_pulse_reset();
}

static void musashi_set_state(void *c, const struct m68k_state *s)
{
    int i;

    (void)c;

    /*
     * Set SR first: m68ki_set_sr() updates the S/trace/interrupt flags and
     * may swap the active stack pointer.  The USP/ISP writes below then
     * establish the exact architectural stack pointers.
     */
    m68k_set_reg(M68K_REG_SR, s->sr);

    for (i = 0; i < 8; i++)
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), s->d[i]);
    for (i = 0; i < 7; i++)
        m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), s->a[i]);

    m68k_set_reg(M68K_REG_USP, s->usp);
    m68k_set_reg(M68K_REG_ISP, s->ssp);
    m68k_set_reg(M68K_REG_PC, s->pc);

    m68k_set_irq(0);
    cpu.stopped = 0;
}

static void musashi_get_state(void *c, struct m68k_state *s)
{
    int i;

    (void)c;

    for (i = 0; i < 8; i++)
        s->d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
    for (i = 0; i < 7; i++)
        s->a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));

    s->usp = m68k_get_reg(NULL, M68K_REG_USP);
    s->ssp = m68k_get_reg(NULL, M68K_REG_ISP);
    s->sr = m68k_get_reg(NULL, M68K_REG_SR);
    s->pc = m68k_get_reg(NULL, M68K_REG_PC);
    s->stopped = (cpu.stopped & STOP_LEVEL_STOP) != 0;

    /*
     * The SST vectors report the address of the STOP instruction itself,
     * while Musashi leaves PC after the immediate word.  Match the reference
     * Cyclone adapter (and the vectors) by backing up over the STOP opcode.
     */
    if (s->stopped)
        s->pc -= 4;
}

static int musashi_step(void *c)
{
    (void)c;
    return m68k_execute(1);
}

static int musashi_stopped(void *c)
{
    (void)c;
    return (cpu.stopped & STOP_LEVEL_STOP) != 0;
}

struct m68k_core *musashi_core_create(void)
{
    struct m68k_core *core;

    memset(&cpu, 0, sizeof(cpu));

    m68k_set_context(&cpu);
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(musashi_int_ack);
    m68k_set_tas_instr_callback(musashi_tas);
    m68k_init();
    m68k_pulse_reset();

    core = calloc(1, sizeof(*core));
    if (!core)
        return NULL;

    core->name = "Musashi";
    core->ctx = &cpu;
    core->destroy = musashi_destroy;
    core->reset = musashi_reset;
    core->set_state = musashi_set_state;
    core->get_state = musashi_get_state;
    core->step = musashi_step;
    core->stopped = musashi_stopped;
    return core;
}
