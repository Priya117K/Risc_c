/*
 * cpu.h - RV32I hart state and the fetch/decode/execute engine.
 *
 * Fault model
 * -----------
 * Any illegal condition (bad fetch, bad load/store address, misalignment,
 * illegal opcode, ebreak) stops the hart with state == CPU_TRAPPED. Traps are
 * *precise*: pc still points at the faulting instruction, no architectural
 * state (registers, memory, pc) from that instruction has been committed, and
 * trap_value holds the offending address / raw instruction word.
 */
#ifndef RISKV_CPU_H
#define RISKV_CPU_H

#include <stdint.h>
#include <stdio.h>
#include "decode.h"
#include "memory.h"
    
typedef enum {
    TRAP_NONE = 0,
    TRAP_FETCH_MISALIGNED,
    TRAP_FETCH_FAULT,
    TRAP_ILLEGAL_INSTR,
    TRAP_BRANCH_MISALIGNED,   /* taken branch / jump to a non-4-byte-aligned target */
    TRAP_LOAD_MISALIGNED,
    TRAP_LOAD_FAULT,
    TRAP_STORE_MISALIGNED,
    TRAP_STORE_FAULT,
    TRAP_EBREAK
} trap_t;

typedef enum { CPU_RUNNING, CPU_HALTED, CPU_TRAPPED } cpu_state_t;

/* Linux RISC-V ABI syscall numbers supported via ecall (number in a7). */
#define SYS_WRITE 64
#define SYS_EXIT  93

typedef struct {
    uint32_t    x[32];          /* integer register file; x[0] is hard-wired to 0 */
    uint32_t    pc;
    memory_t    mem;

    cpu_state_t state;
    trap_t      trap;
    uint32_t    trap_value;
    int         exit_code;      /* valid when state == CPU_HALTED */

    /* performance / state tracking */
    uint64_t    instret;
    uint64_t    class_count[CLS_COUNT];
    uint64_t    branches_taken;
    uint64_t    loads_bytes, stores_bytes;

    FILE       *trace;          /* if non-NULL, one line per executed instruction */
} cpu_t;

/* Allocates `size` bytes of guest RAM at `base`; pc = base; sp = top of RAM. */
int  cpu_init(cpu_t *c, uint32_t base, uint32_t size);
void cpu_free(cpu_t *c);

/* Execute exactly one instruction (no-op unless state == CPU_RUNNING). */
void cpu_step(cpu_t *c);

/* Run until halt/trap, or until `max_instr` have retired (0 = unlimited). */
void cpu_run(cpu_t *c, uint64_t max_instr);

const char *trap_name(trap_t t);
void cpu_dump_regs(FILE *f, const cpu_t *c);
void cpu_report(FILE *f, const cpu_t *c);       /* one-line halt / trap summary */
void cpu_print_stats(FILE *f, const cpu_t *c);

#endif
