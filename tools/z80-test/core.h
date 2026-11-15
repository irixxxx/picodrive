/*
 * Shared definitions for the external z80 single-step test harness.
 *
 * Test data: https://github.com/SingleStepTests/z80
 *
 * The harness reads the packed .json.bin format produced by prepare-tests.py,
 * so no JSON parser is needed at run time.
 */
#ifndef Z80_TEST_CORE_H
#define Z80_TEST_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * CZ80 fetches opcode bytes and immediates by pointer arithmetic, so a fetch
 * at 0xFFFF reads one byte past the 64K space instead of wrapping to 0.  The
 * buffer is extended by a page and its first bytes are mirrored at the end so
 * that the 16-bit address wrap of the real cpu is modelled.
 */
#define Z80_MEM_SIZE  0x10000u
#define Z80_MEM_EXTRA 0x100u

/*
 * The z80 architectural state a test vector describes.
 *
 * The SST files also carry WZ (MEMPTR), Q and the EI-delay latch.  They are
 * stored here for completeness but are not compared, because neither core
 * models them fully.
 */
struct z80_state {
    uint16_t af, bc, de, hl, ix, iy, sp, pc, wz;
    uint16_t af2, bc2, de2, hl2;
    uint8_t i, r, im, iff1, iff2, p, q, ei;
};

struct z80_core {
    const char *name;
    void *ctx;
    void (*destroy)(void *ctx);
    void (*reset)(void *ctx);
    void (*set_state)(void *ctx, const struct z80_state *s);
    void (*get_state)(void *ctx, struct z80_state *s);
    int  (*step)(void *ctx);
};

/* One expected I/O transaction from the test vector. */
struct z80_port {
    uint16_t port;
    uint8_t value;
    uint8_t is_write;
    uint8_t used;
};

#define Z80_MAX_PORTS 8

extern uint8_t memory[Z80_MEM_SIZE + Z80_MEM_EXTRA];
extern uint8_t expected_memory[Z80_MEM_SIZE];

/* The I/O bus as described by the test vector. */
extern struct z80_port z80_ports[Z80_MAX_PORTS];
extern int z80_port_count;
extern const char *z80_port_error;

static inline uint8_t mem_read8(uint32_t a)
{
    return memory[a & (Z80_MEM_SIZE - 1)];
}

static inline void mem_write8(uint32_t a, uint8_t v)
{
    memory[a & (Z80_MEM_SIZE - 1)] = v;
}

static inline uint16_t mem_read16(uint32_t a)
{
    uint32_t a0 = a & (Z80_MEM_SIZE - 1);

    return (uint16_t)(memory[a0] |
                      ((uint16_t)memory[(a0 + 1) & (Z80_MEM_SIZE - 1)] << 8));
}

uint8_t z80_port_in(uint16_t port);
void z80_port_out(uint16_t port, uint8_t value);

struct z80_core *cz80_core_create(void);
struct z80_core *drz80_core_create(void);

#endif /* Z80_TEST_CORE_H */
