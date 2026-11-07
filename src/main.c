/*
 * RISK-V : an RV32I instruction-set simulator.
 *
 * Exit status: the guest's exit code (low 8 bits) if it called exit();
 *              134 if the hart trapped; 124 if -n limit was hit;
 *              1 on usage / load errors.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "debugger.h"
#include "loader.h"

#define DEFAULT_BASE   0x80000000u
#define DEFAULT_MEM_MB 16u

static void usage(FILE *f)
{
    fputs("RISK-V - RV32I instruction set simulator\n"
          "\n"
          "usage: riskv [options] <program>\n"
          "\n"
          "  <program>        ELF32 (RISC-V), flat binary, or .hex text file\n"
          "\n"
          "options:\n"
          "  -t, --trace      print each executed instruction to stderr\n"
          "  -d, --dump       dump registers when the program stops\n"
          "  -s, --stats      print execution statistics when the program stops\n"
          "  -i, --interactive  start the interactive debugger\n"
          "  -n <count>       stop after <count> instructions (infinite-loop guard)\n"
          "  -m <MiB>         guest memory size in MiB, 1..1024   (default 16)\n"
          "  -b <addr>        guest memory base address           (default 0x80000000)\n"
          "  -f <fmt>         force format: bin | hex | elf       (default: auto)\n"
          "  -h, --help       show this help\n", f);
}

static const char *need_arg(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "riskv: option '%s' needs an argument\n", argv[*i]);
        return NULL;
    }
    return argv[++*i];
}

static bool parse_ull(const char *s, unsigned long long *out)
{
    char *end;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s || *end != '\0')
        return false;
    *out = v;
    return true;
}

int main(int argc, char **argv)
{
    uint32_t base = DEFAULT_BASE;
    unsigned long long mem_mb = DEFAULT_MEM_MB, max_instr = 0, tmp;
    bool trace = false, dump = false, stats = false, interactive = false;
    load_format_t fmt = LOAD_AUTO;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "-t") || !strcmp(a, "--trace")) {
            trace = true;
        } else if (!strcmp(a, "-d") || !strcmp(a, "--dump")) {
            dump = true;
        } else if (!strcmp(a, "-s") || !strcmp(a, "--stats")) {
            stats = true;
        } else if (!strcmp(a, "-i") || !strcmp(a, "--interactive")) {
            interactive = true;
        } else if (!strcmp(a, "-n")) {
            if (!(v = need_arg(argc, argv, &i)) || !parse_ull(v, &max_instr)) {
                fprintf(stderr, "riskv: bad value for -n\n");
                return 1;
            }
        } else if (!strcmp(a, "-m")) {
            if (!(v = need_arg(argc, argv, &i)) || !parse_ull(v, &mem_mb) ||
                mem_mb < 1 || mem_mb > 1024) {
                fprintf(stderr, "riskv: -m expects 1..1024 (MiB)\n");
                return 1;
            }
        } else if (!strcmp(a, "-b")) {
            if (!(v = need_arg(argc, argv, &i)) || !parse_ull(v, &tmp) ||
                tmp > 0xFFFFFFFFull || (tmp & 3u)) {
                fprintf(stderr, "riskv: -b expects a 4-byte-aligned 32-bit address\n");
                return 1;
            }
            base = (uint32_t)tmp;
        } else if (!strcmp(a, "-f")) {
            if (!(v = need_arg(argc, argv, &i)))
                return 1;
            if (!strcmp(v, "bin"))      fmt = LOAD_BIN;
            else if (!strcmp(v, "hex")) fmt = LOAD_HEX;
            else if (!strcmp(v, "elf")) fmt = LOAD_ELF;
            else {
                fprintf(stderr, "riskv: unknown format '%s'\n", v);
                return 1;
            }
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "riskv: unknown option '%s'\n", a);
            usage(stderr);
            return 1;
        } else if (!path) {
            path = a;
        } else {
            fprintf(stderr, "riskv: more than one program given\n");
            return 1;
        }
    }

    if (!path) {
        usage(stderr);
        return 1;
    }

    cpu_t cpu;
    if (cpu_init(&cpu, base, (uint32_t)(mem_mb << 20)) != 0) {
        fprintf(stderr, "riskv: cannot allocate %llu MiB at base 0x%08x\n",
                mem_mb, (unsigned)base);
        return 1;
    }

    char err[256];
    if (load_program(&cpu, path, fmt, err, sizeof err) != 0) {
        fprintf(stderr, "riskv: %s\n", err);
        cpu_free(&cpu);
        return 1;
    }

    if (trace)
        cpu.trace = stderr;

    if (interactive)
        debugger_run(&cpu);
    else
        cpu_run(&cpu, max_instr);

    int rc = 0;
    switch (cpu.state) {
    case CPU_HALTED:
        rc = cpu.exit_code & 0xff;
        if (stats)
            cpu_report(stderr, &cpu);
        break;
    case CPU_TRAPPED:
        cpu_report(stderr, &cpu);
        rc = 134;
        break;
    case CPU_RUNNING:
        if (!interactive) {
            fprintf(stderr, "[riskv] instruction limit (%llu) reached\n", max_instr);
            rc = 124;
        }
        break;
    }

    if (dump)
        cpu_dump_regs(stderr, &cpu);
    if (stats)
        cpu_print_stats(stderr, &cpu);

    cpu_free(&cpu);
    return rc;
}
