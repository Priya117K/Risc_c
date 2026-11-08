#include "debugger.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disasm.h"

#define MAX_BREAKPOINTS 16

static void print_next(const cpu_t *c)
{
    uint32_t raw;
    if (mem_read(&c->mem, c->pc, 4, &raw) == MEM_OK) {
        char text[64];
        instr_t in = decode(raw);
        disasm(&in, c->pc, text, sizeof text);
        printf("=> 0x%08x:  %08x  %s\n", (unsigned)c->pc, (unsigned)raw, text);
    } else {
        printf("=> 0x%08x:  <unreadable>\n", (unsigned)c->pc);
    }
}

static bool parse_u32(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s || *end != '\0' || v > 0xFFFFFFFFul)
        return false;
    *out = (uint32_t)v;
    return true;
}

static bool is_bp(const uint32_t *bps, int n, uint32_t pc)
{
    for (int i = 0; i < n; i++)
        if (bps[i] == pc)
            return true;
    return false;
}

static void show_state(cpu_t *c)
{
    if (c->state != CPU_RUNNING)
        cpu_report(stdout, c);
    else
        print_next(c);
}

static void print_help(void)
{
    puts("commands:\n"
         "  s [n]         step n instructions (default 1)\n"
         "  c             continue until breakpoint, exit or trap\n"
         "  b <addr>      set breakpoint          d <addr>  delete breakpoint\n"
         "  bl            list breakpoints\n"
         "  r             dump registers          stats     execution statistics\n"
         "  x <addr> [n]  show n 32-bit words at addr (default 4)\n"
         "  q             quit");
}

void debugger_run(cpu_t *c)
{
    uint32_t bps[MAX_BREAKPOINTS];
    int nbp = 0;
    char line[160];

    printf("RISK-V debugger - type 'help' for commands.\n");
    show_state(c);

    for (;;) {
        printf("(riskv) ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin))
            break;

        char *tok[3] = { NULL, NULL, NULL };
        int n = 0;
        for (char *t = strtok(line, " \t\r\n"); t && n < 3; t = strtok(NULL, " \t\r\n"))
            tok[n++] = t;
        if (n == 0)
            continue;
        const char *cmd = tok[0];
        uint32_t v1 = 0, v2 = 0;

        if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
            break;
        } else if (!strcmp(cmd, "h") || !strcmp(cmd, "help")) {
            print_help();
        } else if (!strcmp(cmd, "s") || !strcmp(cmd, "step")) {
            uint32_t count = 1;
            if (n > 1 && !parse_u32(tok[1], &count)) {
                puts("bad count");
                continue;
            }
            for (uint32_t i = 0; i < count && c->state == CPU_RUNNING; i++)
                cpu_step(c);
            show_state(c);
        } else if (!strcmp(cmd, "c") || !strcmp(cmd, "continue")) {
            if (c->state != CPU_RUNNING) {
                show_state(c);
                continue;
            }
            do {
                cpu_step(c);
            } while (c->state == CPU_RUNNING && !is_bp(bps, nbp, c->pc));
            if (c->state == CPU_RUNNING)
                printf("breakpoint hit at 0x%08x\n", (unsigned)c->pc);
            show_state(c);
        } else if (!strcmp(cmd, "b") || !strcmp(cmd, "break")) {
            if (n < 2 || !parse_u32(tok[1], &v1))
                puts("usage: b <addr>");
            else if (nbp == MAX_BREAKPOINTS)
                puts("breakpoint table full");
            else if (!is_bp(bps, nbp, v1)) {
                bps[nbp++] = v1;
                printf("breakpoint %d at 0x%08x\n", nbp, (unsigned)v1);
            }
        } else if (!strcmp(cmd, "d") || !strcmp(cmd, "delete")) {
            if (n < 2 || !parse_u32(tok[1], &v1)) {
                puts("usage: d <addr>");
            } else {
                int found = -1;
                for (int i = 0; i < nbp; i++)
                    if (bps[i] == v1)
                        found = i;
                if (found < 0) {
                    puts("no such breakpoint");
                } else {
                    bps[found] = bps[--nbp];
                    puts("deleted");
                }
            }
        } else if (!strcmp(cmd, "bl")) {
            for (int i = 0; i < nbp; i++)
                printf("  %d: 0x%08x\n", i + 1, (unsigned)bps[i]);
            if (!nbp)
                puts("no breakpoints");
        } else if (!strcmp(cmd, "r") || !strcmp(cmd, "regs")) {
            cpu_dump_regs(stdout, c);
        } else if (!strcmp(cmd, "stats")) {
            cpu_print_stats(stdout, c);
        } else if (!strcmp(cmd, "x")) {
            uint32_t words = 4;
            if (n < 2 || !parse_u32(tok[1], &v1) || (n > 2 && !parse_u32(tok[2], &words))) {
                puts("usage: x <addr> [nwords]");
                continue;
            }
            for (uint32_t i = 0; i < words; i++) {
                uint32_t a = v1 + 4u * i, w;
                if (i % 4 == 0)
                    printf("0x%08x:", (unsigned)a);
                if (mem_read(&c->mem, a, 4, &w) != MEM_OK) {
                    printf(" <inaccessible>\n");
                    goto next_cmd;
                }
                printf(" %08x", (unsigned)w);
                if (i % 4 == 3 || i + 1 == words)
                    putchar('\n');
            }
        } else {
            printf("unknown command '%s' (try 'help')\n", cmd);
        }
next_cmd:
        (void)v2;
    }
}



