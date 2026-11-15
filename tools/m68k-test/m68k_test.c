/*
 * External single-step 68000 test harness for PicoDrive's supported cores:
 * Cyclone (EMU_C68K), Musashi (EMU_M68K) and FAME (EMU_F68K).
 *
 * Test data: https://github.com/SingleStepTests/m68000
 *
 * The binary .json.bin format is read directly; no JSON parser is needed.
 * The format and the comparison model are adapted from the Cyclone-only
 * tester in cpu/cyclone/tests/SingleStepTests/tester.cpp.
 */

#include "core.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

uint8_t memory[MEM_SIZE];
uint8_t final_memory[MEM_SIZE];
uint8_t memory_dirty[DIRTY_PAGES];

struct options {
    int quiet;
    int verbose;
    int max_fail;
    int strict_cycles;
    int run_address_errors;
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

static void clean_memory(void)
{
    unsigned i;

    for (i = 0; i < DIRTY_PAGES; i++) {
        if (memory_dirty[i]) {
            memset(memory + i * DIRTY_PAGE_SIZE, 0, DIRTY_PAGE_SIZE);
            memset(final_memory + i * DIRTY_PAGE_SIZE, 0, DIRTY_PAGE_SIZE);
            memory_dirty[i] = 0;
        }
    }
}

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
    if (magic != UINT32_C(0x89ABCDEF)) {
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
 * Read one state block.  The SST v1 layout is:
 *   d0..d7, a0..a6, usp, ssp, sr, pc, prefetch[2], mem_count,
 *   (address32, data16) * mem_count
 *
 * The pc field is m_au (address + 4), so subtract 4.
 */
static int read_state(FILE *f, struct m68k_state *s, uint8_t *mem)
{
    uint32_t num_bytes, magic, v, mem_count;
    uint16_t w;
    int i;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != UINT32_C(0x01234567)) {
        fprintf(stderr, "bad state magic\n");
        return 0;
    }

    for (i = 0; i < 8; i++) {
        if (!read_u32(f, &v))
            return 0;
        s->d[i] = v;
    }
    for (i = 0; i < 7; i++) {
        if (!read_u32(f, &v))
            return 0;
        s->a[i] = v;
    }
    if (!read_u32(f, &v))
        return 0;
    s->usp = v;
    if (!read_u32(f, &v))
        return 0;
    s->ssp = v;
    if (!read_u32(f, &v))
        return 0;
    s->sr = v;
    if (!read_u32(f, &v))
        return 0;
    s->pc = v - 4;

    /* Prefetch values: not used by the architectural comparison. */
    if (!read_u32(f, &v) || !read_u32(f, &v))
        return 0;

    if (!read_u32(f, &mem_count))
        return 0;
    while (mem_count--) {
        uint32_t a;

        if (!read_u32(f, &a) || !read_u16(f, &w))
            return 0;
        mem_store16(mem, a, w);
    }

    s->stopped = 0;
    return 1;
}

static int read_transactions(FILE *f, uint32_t *cycles, int *has_error)
{
    uint32_t num_bytes, magic, num_transactions, tcyc;
    uint8_t tw;

    *has_error = 0;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != UINT32_C(0x456789AB)) {
        fprintf(stderr, "bad transactions magic\n");
        return 0;
    }
    if (!read_u32(f, cycles) || !read_u32(f, &num_transactions))
        return 0;

    while (num_transactions--) {
        uint32_t fc, addr, data, uds, lds;

        if (!read_u8(f, &tw) || !read_u32(f, &tcyc))
            return 0;
        if (tw == 0)
            continue;
        if (!read_u32(f, &fc) || !read_u32(f, &addr) ||
            !read_u32(f, &data) || !read_u32(f, &uds) ||
            !read_u32(f, &lds))
            return 0;

        /* tw 4/5 are address-error read/write cycles.  The cores do not
         * emulate address errors for normal memory accesses by default,
         * so these tests are skipped unless --address-errors is given. */
        if (tw >= 4)
            *has_error = 1;
    }

    return 1;
}

static int states_equal(const struct m68k_state *e, const struct m68k_state *a)
{
    int i;

    for (i = 0; i < 8; i++)
        if (e->d[i] != a->d[i])
            return 0;
    for (i = 0; i < 7; i++)
        if (e->a[i] != a->a[i])
            return 0;
    if (e->usp != a->usp)
        return 0;
    if (e->ssp != a->ssp)
        return 0;
    if (e->sr != a->sr)
        return 0;
    if (e->pc != a->pc)
        return 0;

    for (i = 0; i < DIRTY_PAGES; i++) {
        if (!memory_dirty[i])
            continue;
        if (memcmp(memory + i * DIRTY_PAGE_SIZE,
                   final_memory + i * DIRTY_PAGE_SIZE,
                   DIRTY_PAGE_SIZE) != 0)
            return 0;
    }

    return 1;
}

static void print_diff(const struct m68k_state *e, const struct m68k_state *a)
{
    int i;
    unsigned shown = 0;

    for (i = 0; i < 8; i++)
        if (e->d[i] != a->d[i])
            printf("  d%d: expected %08x, got %08x\n", i, e->d[i], a->d[i]);
    for (i = 0; i < 7; i++)
        if (e->a[i] != a->a[i])
            printf("  a%d: expected %08x, got %08x\n", i, e->a[i], a->a[i]);
    if (e->usp != a->usp)
        printf("  usp: expected %08x, got %08x\n", e->usp, a->usp);
    if (e->ssp != a->ssp)
        printf("  ssp: expected %08x, got %08x\n", e->ssp, a->ssp);
    if (e->sr != a->sr)
        printf("  sr:  expected %04x, got %04x\n",
               (unsigned)(e->sr & 0xFFFF), (unsigned)(a->sr & 0xFFFF));
    if (e->pc != a->pc)
        printf("  pc:  expected %08x, got %08x\n", e->pc, a->pc);

    for (i = 0; i < DIRTY_PAGES && shown < 8; i++) {
        uint32_t off;

        if (!memory_dirty[i])
            continue;
        for (off = 0; off < DIRTY_PAGE_SIZE && shown < 8; off++) {
            uint32_t addr = i * DIRTY_PAGE_SIZE + off;
            uint8_t m = memory[MEM_BE2(addr)];
            uint8_t f = final_memory[MEM_BE2(addr)];

            if (m != f) {
                printf("  mem[%06x]: expected %02x, got %02x\n",
                       (unsigned)addr, f, m);
                shown++;
            }
        }
    }
    if (shown == 8)
        printf("  ... (more memory differences)\n");
}

static struct m68k_core *create_core(void)
{
#if defined(CORE_CYCLONE)
    return cyclone_core_create();
#elif defined(CORE_FAME)
    return fame_core_create();
#elif defined(CORE_MUSASHI)
    return musashi_core_create();
#else
#error "Define CORE_CYCLONE, CORE_FAME or CORE_MUSASHI"
#endif
}

static int run_test(FILE *f, const char *file_name, struct m68k_core *core,
                    struct stats *st)
{
    uint32_t num_bytes, magic, expected_cycles;
    char name[512];
    struct m68k_state initial, expected, actual;
    int has_error, actual_cycles, state_ok, cycle_ok;

    if (!read_u32(f, &num_bytes) || !read_u32(f, &magic))
        return 0;
    if (magic != UINT32_C(0xABC12367)) {
        fprintf(stderr, "bad test magic in %s\n", file_name);
        return 0;
    }
    if (!read_name(f, name, sizeof(name)))
        return 0;

    clean_memory();
    if (!read_state(f, &initial, memory))
        return 0;
    if (!read_state(f, &expected, final_memory))
        return 0;
    if (!read_transactions(f, &expected_cycles, &has_error))
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
    if (has_error && !opt.run_address_errors) {
        st->skips++;
        return 1;
    }

    core->reset(core->ctx);
    core->set_state(core->ctx, &initial);
    actual_cycles = core->step(core->ctx);
    core->get_state(core->ctx, &actual);

    state_ok = states_equal(&expected, &actual);
    cycle_ok = (actual_cycles == (int)expected_cycles);

    if (!state_ok) {
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

static int run_file(const char *path, struct m68k_core *core, struct stats *total_st)
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

    if (!read_u32(f, &magic) || magic != UINT32_C(0x1A3F5D71)) {
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

static int run_path(const char *path, struct m68k_core *core, struct stats *st)
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
    printf("Runs SingleStepTests/m68000 v1 .json.bin files against one core.\n");
    printf("If no path is given, tests/m68000/v1 is used.\n");
    printf("\n");
    printf("options:\n");
    printf("  -q, --quiet          only print the final summary\n");
    printf("  -v, --verbose        print every failure/warning (default: first 20)\n");
    printf("      --cycles         treat cycle-count mismatches as failures\n");
    printf("      --address-errors run address-error tests instead of skipping\n");
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
    struct m68k_core *core;

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
        } else if (!strcmp(a, "--address-errors")) {
            opt.run_address_errors = 1;
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
        paths[npaths++] = "tests/m68000/v1";

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
