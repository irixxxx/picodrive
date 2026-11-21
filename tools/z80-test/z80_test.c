/*
 * External single-step z80 test harness for PicoDrive's z80 cores.
 *
 * Test data: https://github.com/SingleStepTests/z80
 *
 * prepare-tests.py packs the upstream json vectors into the compact
 * .json.bin format read here, so no JSON parser is needed at run time.
 */

#include "core.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

uint8_t memory[Z80_MEM_SIZE + Z80_MEM_EXTRA];
uint8_t expected_memory[Z80_MEM_SIZE];

struct z80_port z80_ports[Z80_MAX_PORTS];
int z80_port_count;
const char *z80_port_error;

#define MAGIC_FILE  0x5A383054u
#define MAGIC_TEST  0xABC12368u
#define MAGIC_NAME  0x89ABCDEFu
#define MAGIC_STATE 0x01234567u
#define MAGIC_BUS   0x456789ABu

struct options {
    int quiet;
    int verbose;
    int max_fail;
    int strict_cycles;
    int skip_r;
    int list_only;
    const char *filter;
};

struct stats {
    unsigned files;
    unsigned tests;
    unsigned passes;
    unsigned skips;
    unsigned failures;
    unsigned warnings;
    unsigned parse_errors;
};

static struct options opt;
static struct stats total;
static unsigned g_shown_failures;
static unsigned g_shown_warnings;

static int read_u8(FILE *f, uint8_t *v)
{
    uint8_t b;

    if (fread(&b, 1, 1, f) != 1)
        return 0;
    *v = b;
    return 1;
}

static int read_u16(FILE *f, uint16_t *v)
{
    uint8_t b[2];

    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return 0;
    *v = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return 1;
}

static int read_u32(FILE *f, uint32_t *v)
{
    uint8_t b[4];

    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int read_name(FILE *f, char *name, size_t size)
{
    uint32_t num_bytes, magic, len;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != MAGIC_NAME) {
        fprintf(stderr, "bad test name magic\n");
        return 0;
    }
    if (!read_u32(f, &len))
        return 0;
    if (len >= size) {
        fprintf(stderr, "test name too long (%u bytes)\n", len);
        return 0;
    }
    if (fread(name, 1, len, f) != len)
        return 0;
    name[len] = '\0';
    return 1;
}

/*
 * One state block: af, bc, de, hl, ix, iy, sp, pc, wz, then the shadow
 * af/bc/de/hl, then i, r, im, iff1, iff2, p, q, ei, then the ram entries.
 */
static int read_state(FILE *f, struct z80_state *s, uint8_t *mem)
{
    uint32_t num_bytes, magic, count;
    uint16_t addr;
    uint8_t b;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != MAGIC_STATE) {
        fprintf(stderr, "bad state magic\n");
        return 0;
    }

    if (!read_u16(f, &s->af) || !read_u16(f, &s->bc) ||
        !read_u16(f, &s->de) || !read_u16(f, &s->hl) ||
        !read_u16(f, &s->ix) || !read_u16(f, &s->iy) ||
        !read_u16(f, &s->sp) || !read_u16(f, &s->pc) ||
        !read_u16(f, &s->wz))
        return 0;
    if (!read_u16(f, &s->af2) || !read_u16(f, &s->bc2) ||
        !read_u16(f, &s->de2) || !read_u16(f, &s->hl2))
        return 0;
    if (!read_u8(f, &s->i) || !read_u8(f, &s->r) ||
        !read_u8(f, &s->im) || !read_u8(f, &s->iff1) ||
        !read_u8(f, &s->iff2) || !read_u8(f, &s->p) ||
        !read_u8(f, &s->q) || !read_u8(f, &s->ei))
        return 0;

    if (!read_u32(f, &count))
        return 0;
    while (count--) {
        if (!read_u16(f, &addr) || !read_u8(f, &b))
            return 0;
        mem[addr] = b;
    }

    return 1;
}

static int read_bus(FILE *f, uint32_t *cycles)
{
    uint32_t num_bytes, magic, ports;
    uint16_t port;
    uint8_t value, is_write;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != MAGIC_BUS) {
        fprintf(stderr, "bad bus magic\n");
        return 0;
    }
    if (!read_u32(f, cycles) || !read_u32(f, &ports))
        return 0;

    z80_port_count = 0;
    while (ports--) {
        if (!read_u16(f, &port) || !read_u8(f, &value) || !read_u8(f, &is_write))
            return 0;
        if (z80_port_count < Z80_MAX_PORTS) {
            z80_ports[z80_port_count].port = port;
            z80_ports[z80_port_count].value = value;
            z80_ports[z80_port_count].is_write = is_write;
            z80_ports[z80_port_count].used = 0;
            z80_port_count++;
        }
    }

    return 1;
}

static int port_expected(void)
{
    int i;

    for (i = 0; i < z80_port_count; i++)
        if (!z80_ports[i].used)
            return 0;
    return 1;
}

uint8_t z80_port_in(uint16_t port)
{
    int i;

    for (i = 0; i < z80_port_count; i++) {
        if (z80_ports[i].is_write || z80_ports[i].used)
            continue;
        z80_ports[i].used = 1;
        if (z80_ports[i].port != port && !z80_port_error)
            z80_port_error = "IN port mismatch";
        return z80_ports[i].value;
    }
    if (!z80_port_error)
        z80_port_error = "unexpected IN";
    return 0xff;
}

void z80_port_out(uint16_t port, uint8_t value)
{
    int i;

    for (i = 0; i < z80_port_count; i++) {
        if (!z80_ports[i].is_write || z80_ports[i].used)
            continue;
        z80_ports[i].used = 1;
        if (z80_ports[i].port != port && !z80_port_error)
            z80_port_error = "OUT port mismatch";
        else if (z80_ports[i].value != value && !z80_port_error)
            z80_port_error = "OUT value mismatch";
        return;
    }
    if (!z80_port_error)
        z80_port_error = "unexpected OUT";
}

/* WZ, Q and EI are not compared: neither core models them. */
static int states_equal(const struct z80_state *e, const struct z80_state *a)
{
    if (e->af != a->af || e->bc != a->bc || e->de != a->de || e->hl != a->hl)
        return 0;
    if (e->ix != a->ix || e->iy != a->iy)
        return 0;
    if (e->sp != a->sp || e->pc != a->pc)
        return 0;
    if (e->af2 != a->af2 || e->bc2 != a->bc2 ||
        e->de2 != a->de2 || e->hl2 != a->hl2)
        return 0;
    if (e->i != a->i || e->im != a->im)
        return 0;
    if (!opt.skip_r && e->r != a->r)
        return 0;
    if (e->iff1 != a->iff1 || e->iff2 != a->iff2)
        return 0;

    return memcmp(memory, expected_memory, Z80_MEM_SIZE) == 0;
}

static void print_diff(const struct z80_state *e, const struct z80_state *a)
{
    unsigned shown = 0;
    unsigned i;

    if (e->af != a->af)
        printf("  af: expected %04x, got %04x\n", e->af, a->af);
    if (e->bc != a->bc)
        printf("  bc: expected %04x, got %04x\n", e->bc, a->bc);
    if (e->de != a->de)
        printf("  de: expected %04x, got %04x\n", e->de, a->de);
    if (e->hl != a->hl)
        printf("  hl: expected %04x, got %04x\n", e->hl, a->hl);
    if (e->ix != a->ix)
        printf("  ix: expected %04x, got %04x\n", e->ix, a->ix);
    if (e->iy != a->iy)
        printf("  iy: expected %04x, got %04x\n", e->iy, a->iy);
    if (e->sp != a->sp)
        printf("  sp: expected %04x, got %04x\n", e->sp, a->sp);
    if (e->pc != a->pc)
        printf("  pc: expected %04x, got %04x\n", e->pc, a->pc);
    if (e->af2 != a->af2)
        printf("  af': expected %04x, got %04x\n", e->af2, a->af2);
    if (e->bc2 != a->bc2)
        printf("  bc': expected %04x, got %04x\n", e->bc2, a->bc2);
    if (e->de2 != a->de2)
        printf("  de': expected %04x, got %04x\n", e->de2, a->de2);
    if (e->hl2 != a->hl2)
        printf("  hl': expected %04x, got %04x\n", e->hl2, a->hl2);
    if (e->i != a->i)
        printf("  i: expected %02x, got %02x\n", e->i, a->i);
    if (!opt.skip_r && e->r != a->r)
        printf("  r: expected %02x, got %02x\n", e->r, a->r);
    if (e->im != a->im)
        printf("  im: expected %u, got %u\n", e->im, a->im);
    if (e->iff1 != a->iff1)
        printf("  iff1: expected %u, got %u\n", e->iff1, a->iff1);
    if (e->iff2 != a->iff2)
        printf("  iff2: expected %u, got %u\n", e->iff2, a->iff2);

    for (i = 0; i < Z80_MEM_SIZE && shown < 8; i++) {
        if (memory[i] != expected_memory[i]) {
            printf("  mem[%04x]: expected %02x, got %02x\n",
                   i, expected_memory[i], memory[i]);
            shown++;
        }
    }
    if (shown == 8)
        printf("  ... (more memory differences)\n");

    if (z80_port_error)
        printf("  ports: %s\n", z80_port_error);
    else if (!port_expected())
        printf("  ports: expected transaction not performed\n");
}

static struct z80_core *create_core(void)
{
#ifdef CORE_DRZ80
    return drz80_core_create();
#else
    return cz80_core_create();
#endif
}

static int run_test(FILE *f, const char *file_name, struct z80_core *core,
                    struct stats *st)
{
    uint32_t num_bytes, magic, expected_cycles;
    char name[512];
    struct z80_state initial, expected, actual;
    int actual_cycles, state_ok, cycle_ok;
    int ports_ok;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != MAGIC_TEST) {
        fprintf(stderr, "bad test magic in %s\n", file_name);
        return 0;
    }
    if (!read_name(f, name, sizeof(name)))
        return 0;

    memset(memory, 0, sizeof(memory));
    memset(expected_memory, 0, sizeof(expected_memory));
    z80_port_count = 0;
    z80_port_error = NULL;

    if (!read_state(f, &initial, memory))
        return 0;
    memcpy(memory + Z80_MEM_SIZE, memory, Z80_MEM_EXTRA);
    if (!read_state(f, &expected, expected_memory))
        return 0;
    if (!read_bus(f, &expected_cycles))
        return 0;

    st->tests++;

    if (opt.filter && !strstr(name, opt.filter)) {
        st->skips++;
        return 1;
    }
    if (opt.list_only) {
        printf("%s\n", name);
        st->passes++;
        return 1;
    }

    core->reset(core->ctx);
    core->set_state(core->ctx, &initial);
    actual_cycles = core->step(core->ctx);
    core->get_state(core->ctx, &actual);

    state_ok = states_equal(&expected, &actual);
    cycle_ok = (actual_cycles == (int)expected_cycles);
    ports_ok = (z80_port_error == NULL && port_expected());

    if (!state_ok || !ports_ok) {
        st->failures++;
        if (!opt.quiet && (opt.verbose || g_shown_failures < 20)) {
            printf("FAIL: %s [%s]\n", name, file_name);
            print_diff(&expected, &actual);
            g_shown_failures++;
        }
        return 1;
    }

    if (!cycle_ok && opt.strict_cycles) {
        st->failures++;
        if (!opt.quiet && (opt.verbose || g_shown_failures < 20)) {
            printf("FAIL (cycles): %s [%s]\n", name, file_name);
            printf("  cycles: expected %u, got %d\n",
                   expected_cycles, actual_cycles);
            g_shown_failures++;
        }
        return 1;
    }

    st->passes++;
    if (!cycle_ok) {
        st->warnings++;
        if (!opt.quiet && (opt.verbose || g_shown_warnings < 20)) {
            printf("WARN (cycles): %s [%s]: expected %u, got %d\n",
                   name, file_name, expected_cycles, actual_cycles);
            g_shown_warnings++;
        }
    }

    return 1;
}

static int run_file(const char *path, struct z80_core *core, struct stats *total_st)
{
    FILE *f;
    uint32_t magic, num_tests, i;
    struct stats st;

    memset(&st, 0, sizeof(st));

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        total_st->parse_errors++;
        return 0;
    }

    if (!read_u32(f, &magic) || magic != MAGIC_FILE) {
        fprintf(stderr, "%s: bad file magic\n", path);
        fclose(f);
        total_st->parse_errors++;
        return 0;
    }
    if (!read_u32(f, &num_tests)) {
        fprintf(stderr, "%s: truncated file header\n", path);
        fclose(f);
        total_st->parse_errors++;
        return 0;
    }

    st.files = 1;
    if (!opt.quiet)
        printf("Running %u tests from %s\n", num_tests, path);

    for (i = 0; i < num_tests; i++) {
        if (!run_test(f, path, core, &st)) {
            st.parse_errors++;
            break;
        }
        if (opt.max_fail && st.failures >= (unsigned)opt.max_fail)
            break;
    }

    fclose(f);

    if (!opt.quiet)
        printf("  %s: %u passed, %u skipped, %u failed, %u warnings\n",
               path, st.passes, st.skips, st.failures, st.warnings);

    total_st->files += 1;
    total_st->tests += st.tests;
    total_st->passes += st.passes;
    total_st->skips += st.skips;
    total_st->failures += st.failures;
    total_st->warnings += st.warnings;
    total_st->parse_errors += st.parse_errors;

    return st.parse_errors == 0;
}

static int is_test_file(const char *name)
{
    static const char suffix[] = ".json.bin";
    size_t len = strlen(name);

    return len >= sizeof(suffix) - 1 &&
           strcmp(name + len - (sizeof(suffix) - 1), suffix) == 0;
}

static int cmp_str(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;

    return strcmp(*sa, *sb);
}

static int run_path(const char *path, struct z80_core *core, struct stats *st)
{
    struct stat sb;

    if (stat(path, &sb) != 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        st->parse_errors++;
        return 0;
    }

    if (S_ISDIR(sb.st_mode)) {
        DIR *d;
        struct dirent *de;
        char **names = NULL;
        size_t count = 0, cap = 0, i;

        d = opendir(path);
        if (!d) {
            fprintf(stderr, "%s: %s\n", path, strerror(errno));
            st->parse_errors++;
            return 0;
        }

        while ((de = readdir(d)) != NULL) {
            char *full;
            struct stat es;
            size_t len;

            if (de->d_name[0] == '.')
                continue;

            len = strlen(path) + 1 + strlen(de->d_name) + 1;
            full = malloc(len);
            if (!full)
                break;
            snprintf(full, len, "%s/%s", path, de->d_name);

            if (stat(full, &es) == 0 &&
                (S_ISDIR(es.st_mode) || is_test_file(de->d_name))) {
                if (count == cap) {
                    size_t ncap = cap ? cap * 2 : 64;
                    char **n = realloc(names, ncap * sizeof(*names));
                    if (!n) {
                        free(full);
                        break;
                    }
                    names = n;
                    cap = ncap;
                }
                names[count++] = full;
            } else {
                free(full);
            }
        }
        closedir(d);

        qsort(names, count, sizeof(*names), cmp_str);
        for (i = 0; i < count; i++) {
            run_path(names[i], core, st);
            free(names[i]);
            if (opt.max_fail && st->failures >= (unsigned)opt.max_fail)
                break;
        }
        free(names);
        return 1;
    }

    if (!is_test_file(path))
        return 1;

    return run_file(path, core, st);
}

static void print_usage(const char *argv0)
{
    printf("usage: %s [options] [path...]\n", argv0);
    printf("\n");
    printf("Runs SingleStepTests/z80 .json.bin files against CZ80.\n");
    printf("Run prepare-tests.py first to fetch and pack the vectors.\n");
    printf("If no path is given, tests/z80/v1 is used.\n");
    printf("\n");
    printf("options:\n");
    printf("  -q, --quiet          only print the final summary\n");
    printf("  -v, --verbose        print every failure/warning (default: first 20)\n");
    printf("      --cycles         treat cycle-count mismatches as failures\n");
    printf("      --skip-r         do not compare R (DrZ80 does not maintain it)\n");
    printf("      --max-fail=N     stop after N failures (0 = unlimited)\n");
    printf("      --filter=SUBSTR  only run tests whose name contains SUBSTR\n");
    printf("      --list           list test names without executing them\n");
    printf("  -h, --help           show this help\n");
}

int main(int argc, char **argv)
{
    const char *paths[64];
    int npaths = 0;
    int i;
    struct z80_core *core;

    memset(&opt, 0, sizeof(opt));
    memset(&total, 0, sizeof(total));

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
            opt.quiet = 1;
        } else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
            opt.verbose = 1;
        } else if (!strcmp(a, "--cycles")) {
            opt.strict_cycles = 1;
        } else if (!strcmp(a, "--skip-r")) {
            opt.skip_r = 1;
        } else if (!strcmp(a, "--list")) {
            opt.list_only = 1;
        } else if (!strncmp(a, "--max-fail=", 11)) {
            opt.max_fail = atoi(a + 11);
        } else if (!strncmp(a, "--filter=", 9)) {
            opt.filter = a + 9;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", a);
            print_usage(argv[0]);
            return 2;
        } else {
            if (npaths < (int)(sizeof(paths) / sizeof(paths[0])))
                paths[npaths++] = a;
        }
    }

    if (npaths == 0)
        paths[npaths++] = "tests/z80/v1";

    core = create_core();
    if (!core) {
        fprintf(stderr, "failed to create core\n");
        return 2;
    }

    if (!opt.quiet)
        printf("Core: %s\n", core->name);

    for (i = 0; i < npaths; i++) {
        run_path(paths[i], core, &total);
        if (opt.max_fail && total.failures >= (unsigned)opt.max_fail)
            break;
    }

    printf("\nSummary: %u files, %u tests: %u passed, %u skipped, "
           "%u failed, %u warnings\n",
           total.files, total.tests, total.passes, total.skips,
           total.failures, total.warnings);
    if (total.parse_errors)
        printf("%u parse/read errors\n", total.parse_errors);

    core->destroy(core->ctx);
    free(core);

    return (total.failures || total.parse_errors) ? 1 : 0;
}
