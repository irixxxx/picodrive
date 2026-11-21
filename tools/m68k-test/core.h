/*
 * Shared definitions for the external m68k single-step test harness.
 *
 * The harness reads the binary SingleStepTests/m68000 v1 format directly
 * (the same format the Cyclone-only tester in
 * cpu/cyclone/tests/SingleStepTests/tester.cpp reads).
 */
#ifndef M68K_TEST_CORE_H
#define M68K_TEST_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MEM_SIZE        0x1000000u
#define DIRTY_PAGE_SIZE 0x1000u
#define DIRTY_PAGES     (MEM_SIZE / DIRTY_PAGE_SIZE)

/*
 * PicoDrive stores 68000 memory as host-endian 16-bit words: on a
 * little-endian host the two bytes of a 68000 word are swapped in the buffer,
 * and the 68000 byte at address a lives at memory[a ^ 1].  FAME's instruction
 * fetch reads a native u16 straight out of this buffer, so the harness must
 * use the same layout.  Cyclone and Musashi reach memory through the helpers
 * below, which hide the byte swap.
 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define MEM_BE2(a) ((a) ^ 1u)
#else
#define MEM_BE2(a) (a)
#endif

/*
 * A 68000 architectural state as stored by the SingleStepTests format.
 *
 * The test format stores USP and SSP separately, plus the SR.  The active
 * A7 is derived from SR.S when the state is applied to a core:
 *
 *     A7 = (SR & 0x2000) ? SSP : USP
 *
 * pc is already adjusted to the instruction address (the SST file stores
 * m_au, i.e. the address + 4).
 */
struct m68k_state {
    uint32_t d[8];
    uint32_t a[7];
    uint32_t usp;
    uint32_t ssp;
    uint32_t sr;
    uint32_t pc;
    int      stopped;
};

struct m68k_core {
    const char *name;
    void *ctx;
    void (*destroy)(void *ctx);
    void (*reset)(void *ctx);
    void (*set_state)(void *ctx, const struct m68k_state *s);
    void (*get_state)(void *ctx, struct m68k_state *s);
    int  (*step)(void *ctx);
    int  (*stopped)(void *ctx);
};

extern uint8_t memory[MEM_SIZE];
extern uint8_t final_memory[MEM_SIZE];
extern uint8_t memory_dirty[DIRTY_PAGES];

/* Memory is stored in PicoDrive's host-endian 16-bit word layout. */
static inline uint8_t mem_read8(uint32_t a)
{
    return memory[MEM_BE2(a & (MEM_SIZE - 1))];
}

static inline uint16_t mem_read16(uint32_t a)
{
    uint16_t v;

    a &= MEM_SIZE - 2;
    memcpy(&v, memory + a, sizeof(v));
    return v;
}

static inline uint32_t mem_read32(uint32_t a)
{
    return ((uint32_t)mem_read16(a) << 16) | mem_read16(a + 2);
}

static inline void mem_write8(uint32_t a, uint8_t v)
{
    a &= MEM_SIZE - 1;
    memory[MEM_BE2(a)] = v;
    memory_dirty[a / DIRTY_PAGE_SIZE] = 1;
}

static inline void mem_write16(uint32_t a, uint16_t v)
{
    a &= MEM_SIZE - 2;
    memcpy(memory + a, &v, sizeof(v));
    memory_dirty[a / DIRTY_PAGE_SIZE] = 1;
}

static inline void mem_write32(uint32_t a, uint32_t v)
{
    mem_write16(a, (uint16_t)(v >> 16));
    mem_write16(a + 2, (uint16_t)v);
}

/* Store a word into either memory[] or final_memory[] and mark the page dirty. */
static inline void mem_store16(uint8_t *mem, uint32_t a, uint16_t v)
{
    a &= MEM_SIZE - 2;
    memcpy(mem + a, &v, sizeof(v));
    memory_dirty[a / DIRTY_PAGE_SIZE] = 1;
}

struct m68k_core *cyclone_core_create(void);
struct m68k_core *fame_core_create(void);
struct m68k_core *musashi_core_create(void);

#endif /* M68K_TEST_CORE_H */
