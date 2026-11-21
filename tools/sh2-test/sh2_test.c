/*
 * External single-step SH-2 test harness for the MAME interpreter PicoDrive
 * ships in cpu/sh2/mame.
 *
 * Test data: https://github.com/SingleStepTests/sh4
 *
 * The vectors are SH-4 vectors generated from Reicast's interpreter.  Each
 * record holds the state before and after four instructions: a NOP, the
 * instruction under test, a delay-slot filler and another NOP, with the
 * instruction stream at the initial PC.  The harness runs the same four
 * instructions and compares the architectural state and the one data access
 * the reference run recorded.
 *
 * The SH-4-only encodings are skipped: the SH-2 disassembler in the same
 * directory answers whether an encoding exists on the SH-2, and the four
 * encodings the vector README lists as SH-2 incompatible are skipped by
 * opcode.
 */

#include "core.h"
#include "sh2dasm.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RECORD_SIZE	788
#define STATE_SIZE	284
#define CYCLES_SIZE	188
#define OPCODES_SIZE	28

struct options {
	int quiet;
	int verbose;
	int max_fail;
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

static int read_u32(FILE *f, uint32_t *v)
{
	unsigned char b[4];

	if (fread(b, 1, sizeof(b), f) != sizeof(b))
		return 0;
	*v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	     ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return 1;
}

static int read_u64(FILE *f, uint64_t *v)
{
	uint32_t lo, hi;

	if (!read_u32(f, &lo) || !read_u32(f, &hi))
		return 0;
	*v = (uint64_t)hi << 32 | lo;
	return 1;
}

/* state block: u32 size, u32 unused, 69 u32 */
static int read_state(FILE *f, struct sh2_state *s)
{
	uint32_t size, pad, v[69];
	unsigned i;

	if (!read_u32(f, &size) || !read_u32(f, &pad))
		return 0;
	if (size != STATE_SIZE)
		return 0;
	for (i = 0; i < 69; i++)
		if (!read_u32(f, &v[i]))
			return 0;

	for (i = 0; i < 16; i++)
		s->r[i] = v[i];
	for (i = 0; i < 8; i++)
		s->rbank[i] = v[16 + i];
	s->pc = v[56];
	s->gbr = v[57];
	s->sr = v[58];
	s->ssr = v[59];
	s->spc = v[60];
	s->vbr = v[61];
	s->sgr = v[62];
	s->dbr = v[63];
	s->macl = v[64];
	s->mach = v[65];
	s->pr = v[66];
	s->fpscr = v[67];
	s->fpul = v[68];
	return 1;
}

/* cycles block: u32 size, u32 unused, u32 count, 4 records of 44 bytes */
static int read_cycles(FILE *f, struct sh2_test *t)
{
	uint32_t size, a, b;
	unsigned i;

	if (!read_u32(f, &size))
		return 0;
	if (size != CYCLES_SIZE)
		return 0;
	if (!read_u32(f, &a) || !read_u32(f, &b))
		return 0;

	for (i = 0; i < 4; i++) {
		struct sh2_cycle *c = &t->cyc[i];

		/* fetch_val follows fetch_addr; upstream records it wrong and
		 * the harness fetches from opcodes[] instead */
		if (!read_u32(f, &c->actions) ||
		    !read_u32(f, &c->fetch_addr) ||
		    !read_u32(f, &a) ||
		    !read_u32(f, &c->write_addr) ||
		    !read_u64(f, &c->write_val) ||
		    !read_u32(f, &c->write_size) ||
		    !read_u32(f, &c->read_addr) ||
		    !read_u64(f, &c->read_val) ||
		    !read_u32(f, &c->read_size))
			return 0;
	}
	return 1;
}

/* opcodes block: u32 size, u32 unused, 5 u32 */
static int read_opcodes(FILE *f, struct sh2_test *t)
{
	uint32_t size, pad;
	unsigned i;

	if (!read_u32(f, &size) || !read_u32(f, &pad))
		return 0;
	if (size != OPCODES_SIZE)
		return 0;
	for (i = 0; i < SH2_TEST_OPCODES; i++)
		if (!read_u32(f, &t->opcodes[i]))
			return 0;
	return 1;
}

static int read_test(FILE *f, struct sh2_test *t, const char *file,
		     unsigned index)
{
	uint32_t size;

	memset(t, 0, sizeof(*t));
	if (!read_u32(f, &size))
		return 0;
	if (size != RECORD_SIZE)
		return 0;
	if (!read_state(f, &t->initial) ||
	    !read_state(f, &t->final) ||
	    !read_cycles(f, t) ||
	    !read_opcodes(f, t))
		return 0;

	snprintf(t->name, sizeof(t->name), "%s[%u]", file, index);
	return 1;
}

static void mnemonic(const struct sh2_test *t, char *buff, size_t size)
{
	char tmp[128];
	char *p, *end;

	DasmSH2(tmp, t->initial.pc, (unsigned short)t->opcodes[1]);
	p = tmp;
	while (*p == ' ' || *p == '\t')
		p++;
	end = p + strlen(p);
	while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	snprintf(buff, size, "%s", p);
}

/*
 * SH-2 availability.
 *
 * The vectors are SH-4 vectors, so the encoding under test is checked
 * against the SH-2 instruction set rather than against whatever the SH-4
 * core happened to decode.  The SH-2 disassembler is only used for the
 * mnemonic in the failure report: it masks out bits 7-4, so it accepts
 * SH-3/SH-4 control-register forms as if they were SH-2 instructions.
 *
 * The four encodings the vector README lists as SH-2 incompatible are
 * skipped as well, because their SH-4 semantics differ from the SH-2:
 * RTE, the two LDC-to-SR forms and TRAPA.
 */
static int sh2_single_reg(unsigned op)
{
	switch (op & 0x00ff) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x05: case 0x06: case 0x07:
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0e:
	case 0x10: case 0x11: case 0x12: case 0x13:
	case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1a: case 0x1b:
	case 0x1e:
	case 0x20: case 0x21: case 0x22: case 0x23:
	case 0x24: case 0x25: case 0x26: case 0x27:
	case 0x28: case 0x29: case 0x2a: case 0x2b:
	case 0x2e:
		return 1;
	}
	return 0;
}

static int is_sh2_encoding(const struct sh2_test *t)
{
	unsigned op = t->opcodes[1] & 0xffff;
	unsigned nib = op & 0x0f;

	/* SH-2 incompatible per the vector README */
	if ((op & 0xff00) == 0xc300)	/* TRAPA #imm */
		return 0;
	if ((op & 0xf0ff) == 0x4007)	/* LDC.L @Rm+,SR */
		return 0;
	if ((op & 0xf0ff) == 0x400e)	/* LDC Rm,SR */
		return 0;
	if ((op & 0xf0ff) == 0x002b)	/* RTE */
		return 0;

	switch (op >> 12) {
	case 0x0:
		if (nib == 0x04 || nib == 0x05 || nib == 0x06 ||
		    nib == 0x07 || nib == 0x0c || nib == 0x0d ||
		    nib == 0x0e || nib == 0x0f)
			return 1;	/* @(R0,Rm) moves, MUL.L, MAC.L */
		switch (op & 0x00ff) {
		case 0x02: case 0x03:	/* STC SR,Rn; BSRF */
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x12: case 0x18: case 0x19: case 0x1a:
		case 0x1b: case 0x22: case 0x23: case 0x28:
		case 0x29: case 0x2a:
			return 1;
		}
		return 0;
	case 0x1:			/* MOV.L Rm,@(disp,Rn) */
		return 1;
	case 0x2:
		return nib != 0x03;
	case 0x3:
		return nib != 0x01 && nib != 0x09;
	case 0x4:
		if (nib == 0x0f)	/* MAC.W @Rm+,@Rn+ */
			return 1;
		/* 0x4nnC/0x4nnD are SHAD/SHLD on SH-2A and later; this SH-2 core
		 * and the SH-2 disassembler both treat them as illegal */
		return sh2_single_reg(op);
	case 0x5:			/* MOV.L @(disp,Rm),Rn */
	case 0x6:			/* MOV.B/W/L forms */
	case 0x7:			/* ADDI #imm,Rn */
		return 1;
	case 0x8:
		switch (op >> 8) {
		case 0x80: case 0x81: case 0x82:
		case 0x84: case 0x85: case 0x86:
		case 0x88: case 0x89: case 0x8b:
		case 0x8d: case 0x8f:
			return 1;
		}
		return 0;
	case 0x9:			/* MOV.W @(disp,PC),Rn */
		return 1;
	case 0xa:			/* BRA */
	case 0xb:			/* BSR */
		return 1;
	case 0xc:
		switch (op >> 8) {
		case 0xc0: case 0xc1: case 0xc2:
		case 0xc4: case 0xc5: case 0xc6:
		case 0xc8: case 0xc9: case 0xca: case 0xcb:
		case 0xcc: case 0xcd: case 0xce: case 0xcf:
			return 1;
		}
		return 0;
	case 0xd:			/* MOV.L @(disp,PC),Rn */
	case 0xe:			/* MOV #imm,Rn */
		return 1;
	}
	return 0;			/* 0xf: FPU */
}

static void print_state_diff(const struct sh2_test *t,
			     const struct sh2_state *got)
{
	unsigned i;

	for (i = 0; i < 16; i++)
		if (t->final.r[i] != got->r[i])
			printf("  r%u: expected %08x, got %08x\n", i,
			       t->final.r[i], got->r[i]);
	if (t->final.pc != got->pc)
		printf("  pc: expected %08x, got %08x\n",
		       t->final.pc, got->pc);
	if (t->final.sr != got->sr)
		printf("  sr: expected %08x, got %08x\n",
		       t->final.sr, got->sr);
	if (t->final.pr != got->pr)
		printf("  pr: expected %08x, got %08x\n",
		       t->final.pr, got->pr);
	if (t->final.gbr != got->gbr)
		printf("  gbr: expected %08x, got %08x\n",
		       t->final.gbr, got->gbr);
	if (t->final.vbr != got->vbr)
		printf("  vbr: expected %08x, got %08x\n",
		       t->final.vbr, got->vbr);
	if (t->final.mach != got->mach)
		printf("  mach: expected %08x, got %08x\n",
		       t->final.mach, got->mach);
	if (t->final.macl != got->macl)
		printf("  macl: expected %08x, got %08x\n",
		       t->final.macl, got->macl);
}

static void print_bus_diff(const struct sh2_test *t)
{
	int i;

	if (sh2_bus.addr_errors) {
		printf("  bus: unexpected %s %08x/%d,",
		       sh2_bus.last_is_write ? "write" : "read",
		       sh2_bus.last_addr, sh2_bus.last_size);
		for (i = 0; i < 4; i++) {
			const struct sh2_cycle *c = &t->cyc[i];

			if (c->actions & 1)
				printf(" expected read %08x/%u", c->read_addr,
				       c->read_size);
			if (c->actions & 2)
				printf(" expected write %08x/%u", c->write_addr,
				       c->write_size);
		}
		printf("\n");
	}
	if (sh2_bus.value_errors)
		printf("  bus: write value mismatch\n");
	if (sh2_bus.have_read && !sh2_bus.reads)
		printf("  bus: expected read at %08x never happened\n",
		       sh2_bus.read_addr);
	if (sh2_bus.have_write && !sh2_bus.writes)
		printf("  bus: expected write at %08x never happened\n",
		       sh2_bus.write_addr);
}

static int run_test(FILE *f, const char *file, unsigned index,
		    struct stats *st)
{
	struct sh2_test t;
	struct sh2_state got;
	char mn[128];
	int n, failed;

	if (!read_test(f, &t, file, index))
		return 0;

	st->tests++;

	if (!is_sh2_encoding(&t)) {
		st->skips++;
		return 1;
	}
	if (opt.filter && !strstr(t.name, opt.filter)) {
		st->skips++;
		return 1;
	}
	if (opt.list_only) {
		mnemonic(&t, mn, sizeof(mn));
		printf("%s  %s\n", mn, t.name);
		st->passes++;
		return 1;
	}

	sh2_bus_begin(&t);
	sh2_core_load(&t.initial);
	for (n = 0; n < 4; )
		n += sh2_core_step();
	sh2_core_get(&got);

	failed = 0;
	if (memcmp(t.final.r, got.r, sizeof(got.r)) != 0 ||
	    t.final.pc != got.pc || t.final.sr != got.sr ||
	    t.final.pr != got.pr || t.final.gbr != got.gbr ||
	    t.final.vbr != got.vbr || t.final.mach != got.mach ||
	    t.final.macl != got.macl)
		failed = 1;
	if (sh2_bus.addr_errors || sh2_bus.value_errors)
		failed = 1;
	if (sh2_bus.have_read && !sh2_bus.reads)
		failed = 1;
	if (sh2_bus.have_write && !sh2_bus.writes)
		failed = 1;

	if (failed) {
		st->failures++;
		if (!opt.quiet && (opt.verbose || g_shown_failures < 20)) {
			mnemonic(&t, mn, sizeof(mn));
			printf("FAIL: %s  %s\n", t.name, mn);
			print_state_diff(&t, &got);
			print_bus_diff(&t);
			g_shown_failures++;
		}
		return 1;
	}

	st->passes++;
	if (sh2_bus.size_errors) {
		st->warnings++;
		if (!opt.quiet && (opt.verbose || g_shown_warnings < 20)) {
			mnemonic(&t, mn, sizeof(mn));
			printf("WARN (size): %s  %s\n", t.name, mn);
			g_shown_warnings++;
		}
	}
	return 1;
}

static int run_file(const char *path, struct stats *total_st)
{
	FILE *f;
	uint32_t size, count, i;
	struct stats st;
	char name[256];
	const char *base;

	memset(&st, 0, sizeof(st));

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		total_st->parse_errors++;
		return 0;
	}

	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(name, sizeof(name), "%s", base);

	fseek(f, 0, SEEK_END);
	size = (uint32_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size % RECORD_SIZE != 0) {
		fprintf(stderr, "%s: %u bytes is not a multiple of %u\n",
			path, size, RECORD_SIZE);
		fclose(f);
		total_st->parse_errors++;
		return 0;
	}
	count = size / RECORD_SIZE;

	st.files = 1;
	if (!opt.quiet)
		printf("Running %u tests from %s\n", count, path);

	for (i = 0; i < count; i++) {
		if (!run_test(f, name, i, &st)) {
			st.parse_errors++;
			break;
		}
		if (opt.max_fail && st.failures >= (unsigned)opt.max_fail)
			break;
	}

	fclose(f);

	if (!opt.quiet)
		printf("  %s: %u passed, %u skipped, %u failed, %u warnings\n",
		       name, st.passes, st.skips, st.failures, st.warnings);

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

static int run_path(const char *path, struct stats *st)
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
					char **n = realloc(names,
							   ncap * sizeof(*names));
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
			run_path(names[i], st);
			free(names[i]);
			if (opt.max_fail && st->failures >= (unsigned)opt.max_fail)
				break;
		}
		free(names);
		return 1;
	}

	if (!is_test_file(path))
		return 1;

	return run_file(path, st);
}

static void print_usage(const char *argv0)
{
	printf("usage: %s [options] [path...]\n", argv0);
	printf("\n");
	printf("Runs SingleStepTests/sh4 .json.bin files against the MAME SH-2\n");
	printf("interpreter, skipping encodings the SH-2 does not have.\n");
	printf("If no path is given, tests/sh4/v1 is used.\n");
	printf("\n");
	printf("options:\n");
	printf("  -q, --quiet          only print the final summary\n");
	printf("  -v, --verbose        print every failure/warning (default: first 20)\n");
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

	memset(&opt, 0, sizeof(opt));
	memset(&total, 0, sizeof(total));

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			opt.quiet = 1;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			opt.verbose = 1;
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
		paths[npaths++] = "tests/sh4/v1";

	sh2_core_init();

	if (!opt.quiet)
		printf("Core: %s\n", sh2_core_name);

	for (i = 0; i < npaths; i++) {
		run_path(paths[i], &total);
		if (opt.max_fail && total.failures >= (unsigned)opt.max_fail)
			break;
	}

	printf("\nSummary: %u files, %u tests: %u passed, %u skipped, "
	       "%u failed, %u warnings\n",
	       total.files, total.tests, total.passes, total.skips,
	       total.failures, total.warnings);
	if (total.parse_errors)
		printf("%u parse/read errors\n", total.parse_errors);

	return (total.failures || total.parse_errors) ? 1 : 0;
}
