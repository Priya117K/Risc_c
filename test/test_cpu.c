/*
 * Self-contained test suite: tiny assembler macros build RV32I programs in C,
 * which are executed on the simulator and checked against expected state.
 * Ends with a randomised fuzzer that hammers decode + execute with arbitrary
 * instruction words and register values (run it under ASan: make asan-test).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "decode.h"
#include "disasm.h"
#include "memory.h"

#define BASE 0x80000000u

static int tests_run, tests_failed;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { tests_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(a, b) do { \
    tests_run++; \
    if (strcmp((a), (b)) != 0) { tests_failed++; \
        printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } \
} while (0)

/* ---------------------------------------------------------------- encoders */
static uint32_t enc_r(unsigned f7, unsigned rs2, unsigned rs1, unsigned f3, unsigned rd, unsigned opc)
{
    return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | opc;
}
static uint32_t enc_i(int32_t imm, unsigned rs1, unsigned f3, unsigned rd, unsigned opc)
{
    return (((uint32_t)imm & 0xfffu) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | opc;
}
static uint32_t enc_s(int32_t imm, unsigned rs2, unsigned rs1, unsigned f3, unsigned opc)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 5) & 0x7fu) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | ((u & 0x1fu) << 7) | opc;
}
static uint32_t enc_b(int32_t imm, unsigned rs2, unsigned rs1, unsigned f3)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 12) & 1u) << 31) | (((u >> 5) & 0x3fu) << 25) | (rs2 << 20) | (rs1 << 15) |
           (f3 << 12) | (((u >> 1) & 0xfu) << 8) | (((u >> 11) & 1u) << 7) | 0x63u;
}
static uint32_t enc_u(uint32_t imm20, unsigned rd, unsigned opc)
{
    return (imm20 << 12) | (rd << 7) | opc;
}
static uint32_t enc_j(int32_t imm, unsigned rd)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3ffu) << 21) | (((u >> 11) & 1u) << 20) |
           (((u >> 12) & 0xffu) << 12) | (rd << 7) | 0x6fu;
}

enum { ZERO = 0, RA = 1, SP = 2, T0 = 5, T1 = 6, T2 = 7,
       A0 = 10, A1 = 11, A2 = 12, A3 = 13, A4 = 14, A5 = 15, A7 = 17 };

#define ADDI(rd, rs1, imm)  enc_i(imm, rs1, 0, rd, 0x13)
#define SLTI(rd, rs1, imm)  enc_i(imm, rs1, 2, rd, 0x13)
#define XORI(rd, rs1, imm)  enc_i(imm, rs1, 4, rd, 0x13)
#define SLLI(rd, rs1, sh)   enc_i(sh, rs1, 1, rd, 0x13)
#define SRLI(rd, rs1, sh)   enc_i(sh, rs1, 5, rd, 0x13)
#define SRAI(rd, rs1, sh)   enc_i(0x400 | (sh), rs1, 5, rd, 0x13)
#define ADD(rd, rs1, rs2)   enc_r(0x00, rs2, rs1, 0, rd, 0x33)
#define SUB(rd, rs1, rs2)   enc_r(0x20, rs2, rs1, 0, rd, 0x33)
#define SLL(rd, rs1, rs2)   enc_r(0x00, rs2, rs1, 1, rd, 0x33)
#define SLT(rd, rs1, rs2)   enc_r(0x00, rs2, rs1, 2, rd, 0x33)
#define SLTU(rd, rs1, rs2)  enc_r(0x00, rs2, rs1, 3, rd, 0x33)
#define XOR(rd, rs1, rs2)   enc_r(0x00, rs2, rs1, 4, rd, 0x33)
#define SRA(rd, rs1, rs2)   enc_r(0x20, rs2, rs1, 5, rd, 0x33)
#define OR(rd, rs1, rs2)    enc_r(0x00, rs2, rs1, 6, rd, 0x33)
#define AND(rd, rs1, rs2)   enc_r(0x00, rs2, rs1, 7, rd, 0x33)
#define LB(rd, rs1, imm)    enc_i(imm, rs1, 0, rd, 0x03)
#define LH(rd, rs1, imm)    enc_i(imm, rs1, 1, rd, 0x03)
#define LW(rd, rs1, imm)    enc_i(imm, rs1, 2, rd, 0x03)
#define LBU(rd, rs1, imm)   enc_i(imm, rs1, 4, rd, 0x03)
#define LHU(rd, rs1, imm)   enc_i(imm, rs1, 5, rd, 0x03)
#define SB(rs2, rs1, imm)   enc_s(imm, rs2, rs1, 0, 0x23)
#define SH(rs2, rs1, imm)   enc_s(imm, rs2, rs1, 1, 0x23)
#define SW(rs2, rs1, imm)   enc_s(imm, rs2, rs1, 2, 0x23)
#define BEQ(rs1, rs2, off)  enc_b(off, rs2, rs1, 0)
#define BNE(rs1, rs2, off)  enc_b(off, rs2, rs1, 1)
#define BLT(rs1, rs2, off)  enc_b(off, rs2, rs1, 4)
#define BGEU(rs1, rs2, off) enc_b(off, rs2, rs1, 7)
#define LUI(rd, imm20)      enc_u(imm20, rd, 0x37)
#define AUIPC(rd, imm20)    enc_u(imm20, rd, 0x17)
#define JAL(rd, off)        enc_j(off, rd)
#define JALR(rd, rs1, imm)  enc_i(imm, rs1, 0, rd, 0x67)
#define ECALL               0x00000073u
#define EBREAK              0x00100073u
#define HALT                ADDI(A7, ZERO, 93), ECALL

/* ----------------------------------------------------------------- harness */
static cpu_t cpu;

static void run_prog(const uint32_t *p, size_t n)
{
    cpu_free(&cpu);
    if (cpu_init(&cpu, BASE, 0x10000) != 0) {
        puts("cpu_init failed");
        exit(2);
    }
    for (size_t i = 0; i < n; i++)
        mem_write(&cpu.mem, BASE + 4u * (uint32_t)i, 4, p[i]);
    cpu_run(&cpu, 100000);
}

#define RUN(...) do { \
    const uint32_t p_[] = { __VA_ARGS__ }; \
    run_prog(p_, sizeof p_ / sizeof p_[0]); \
} while (0)

static const char *dis(uint32_t raw, uint32_t pc)
{
    static char buf[80];
    instr_t in = decode(raw);
    disasm(&in, pc, buf, sizeof buf);
    return buf;
}

/* ------------------------------------------------------------------- tests */
static void test_alu(void)
{
    RUN(ADDI(T0, ZERO, -5), ADDI(T1, ZERO, 3),
        ADD(A0, T0, T1), SUB(A1, T0, T1), SLT(A2, T0, T1), SLTU(A3, T0, T1),
        SRAI(A4, T0, 1), SRLI(A5, T0, 28), HALT);
    CHECK(cpu.state == CPU_HALTED);
    CHECK(cpu.x[A0] == 0xFFFFFFFEu);   /* -5 + 3 */
    CHECK(cpu.x[A1] == 0xFFFFFFF8u);   /* -5 - 3 */
    CHECK(cpu.x[A2] == 1);             /* signed:   -5 < 3 */
    CHECK(cpu.x[A3] == 0);             /* unsigned: 0xFFFFFFFB < 3 is false */
    CHECK(cpu.x[A4] == 0xFFFFFFFDu);   /* arithmetic shift keeps sign: -5 >> 1 = -3 */
    CHECK(cpu.x[A5] == 0xFu);          /* logical shift */
}

static void test_logic_and_shifts(void)
{
    RUN(ADDI(T0, ZERO, 0x0f0), ADDI(T1, ZERO, 0x0ff),
        AND(A0, T0, T1), OR(A1, T0, T1), XOR(A2, T0, T1),
        ADDI(T2, ZERO, 33), SLL(A3, T1, T2),      /* shift amount uses low 5 bits: 33 -> 1 */
        ADDI(T2, ZERO, -1), SLLI(A4, T2, 31), SRA(A5, A4, T1), HALT);
    CHECK(cpu.x[A0] == 0x0f0u);
    CHECK(cpu.x[A1] == 0x0ffu);
    CHECK(cpu.x[A2] == 0x00fu);
    CHECK(cpu.x[A3] == 0x1feu);
    CHECK(cpu.x[A4] == 0x80000000u);
    CHECK(cpu.x[A5] == 0xFFFFFFFFu);   /* sra by (0xff & 31) = 31 of 0x80000000 */
}

static void test_x0_hardwired(void)
{
    RUN(ADDI(ZERO, ZERO, 123), ADD(A0, ZERO, ZERO), LUI(ZERO, 0xfffff), HALT);
    CHECK(cpu.x[0] == 0);
    CHECK(cpu.x[A0] == 0);
}

static void test_lui_auipc(void)
{
    RUN(LUI(A0, 0x12345), AUIPC(A1, 1), HALT);
    CHECK(cpu.x[A0] == 0x12345000u);
    CHECK(cpu.x[A1] == BASE + 4u + 0x1000u);
}

static void test_branch_loop(void)
{
    /* sum = 10 + 9 + ... + 1 */
    RUN(ADDI(T0, ZERO, 0), ADDI(T1, ZERO, 10),
        ADD(T0, T0, T1), ADDI(T1, T1, -1), BNE(T1, ZERO, -8),
        ADDI(A0, T0, 0), ADDI(A7, ZERO, 93), ECALL);
    CHECK(cpu.state == CPU_HALTED);
    CHECK(cpu.exit_code == 55);
    CHECK(cpu.instret == 35);
    CHECK(cpu.branches_taken == 9);
    CHECK(cpu.class_count[CLS_BRANCH] == 10);
}

static void test_branch_conditions(void)
{
    RUN(ADDI(T0, ZERO, -1), ADDI(T1, ZERO, 1),
        BLT(T0, T1, 8), ADDI(A0, ZERO, 99),     /* taken: skips the poison write */
        BGEU(T1, T0, 8), ADDI(A1, ZERO, 7),     /* not taken: 1 >= 0xFFFFFFFF is false */
        HALT);
    CHECK(cpu.x[A0] == 0);
    CHECK(cpu.x[A1] == 7);
}

static void test_jal_jalr(void)
{
    RUN(JAL(RA, 8),            /* 0: call 2 */
        JAL(ZERO, 12),         /* 1: after return, jump to 4 */
        ADDI(A0, ZERO, 42),    /* 2: callee */
        JALR(ZERO, RA, 0),     /* 3: ret */
        ADDI(A7, ZERO, 93),    /* 4 */
        ECALL);                /* 5 */
    CHECK(cpu.state == CPU_HALTED);
    CHECK(cpu.x[A0] == 42);
    CHECK(cpu.x[RA] == BASE + 4u);
    CHECK(cpu.instret == 6);
}

static void test_load_store(void)
{
    RUN(LUI(T0, 0x80001), ADDI(T1, ZERO, -1),
        SW(T1, T0, 0), SB(ZERO, T0, 1),                 /* word becomes 0xFFFF00FF */
        LW(A0, T0, 0), LB(A1, T0, 0), LBU(A2, T0, 0),
        LH(A3, T0, 0), LH(A4, T0, 2), LHU(A5, T0, 2), HALT);
    CHECK(cpu.x[A0] == 0xFFFF00FFu);
    CHECK(cpu.x[A1] == 0xFFFFFFFFu);   /* lb sign-extends 0xFF */
    CHECK(cpu.x[A2] == 0xFFu);         /* lbu zero-extends */
    CHECK(cpu.x[A3] == 0x00FFu);
    CHECK(cpu.x[A4] == 0xFFFFFFFFu);   /* lh sign-extends 0xFFFF */
    CHECK(cpu.x[A5] == 0xFFFFu);
    CHECK(cpu.stores_bytes == 5 && cpu.loads_bytes == 4 + 1 + 1 + 2 + 2 + 2);
}

static void test_faults_are_precise(void)
{
    /* load from unmapped address 0 */
    RUN(ADDI(A0, ZERO, 77), LW(A0, ZERO, 0), HALT);
    CHECK(cpu.state == CPU_TRAPPED);
    CHECK(cpu.trap == TRAP_LOAD_FAULT);
    CHECK(cpu.trap_value == 0);
    CHECK(cpu.pc == BASE + 4u);        /* points at the faulting instruction */
    CHECK(cpu.x[A0] == 77);            /* destination not clobbered */
    CHECK(cpu.instret == 1);

    /* store one past the end of memory */
    RUN(LUI(T0, 0x80010), SW(ZERO, T0, 0), HALT);
    CHECK(cpu.trap == TRAP_STORE_FAULT);
    CHECK(cpu.trap_value == BASE + 0x10000u);

    /* the last valid word is writable, the first word past the end is not */
    CHECK(mem_write(&cpu.mem, BASE + 0xFFFCu, 4, 1) == MEM_OK);
    CHECK(mem_write(&cpu.mem, BASE + 0x10000u, 4, 1) == MEM_ERR_OOB);
}

static void test_misalignment_and_wraparound(void)
{
    RUN(LUI(T0, 0x80001), ADDI(T0, T0, 1), SW(ZERO, T0, 0), HALT);
    CHECK(cpu.trap == TRAP_STORE_MISALIGNED);

    RUN(LUI(T0, 0x80001), LH(A0, T0, 1), HALT);
    CHECK(cpu.trap == TRAP_LOAD_MISALIGNED);

    /* with alignment checks off, an access straddling the end must still fail */
    cpu.mem.strict_align = false;
    uint32_t v;
    CHECK(mem_read(&cpu.mem, BASE + 0xFFFCu, 4, &v) == MEM_OK);
    CHECK(mem_read(&cpu.mem, BASE + 0xFFFEu, 4, &v) == MEM_ERR_OOB);
    CHECK(mem_read(&cpu.mem, 0xFFFFFFFEu, 4, &v) == MEM_ERR_OOB);   /* address wrap-around */
    CHECK(mem_read(&cpu.mem, BASE - 1u, 1, &v) == MEM_ERR_OOB);
    CHECK(!mem_in_range(&cpu.mem, BASE, 0x10001ull));
    CHECK(!mem_in_range(&cpu.mem, 0xFFFFFFFFu, 0xFFFFFFFFull));
}

static void test_illegal_and_control_traps(void)
{
    RUN(0x00000000u);
    CHECK(cpu.trap == TRAP_ILLEGAL_INSTR && cpu.trap_value == 0);

    RUN(0xFFFFFFFFu);
    CHECK(cpu.trap == TRAP_ILLEGAL_INSTR);

    RUN(EBREAK);
    CHECK(cpu.trap == TRAP_EBREAK);

    RUN(JAL(ZERO, 2));                 /* target BASE+2 is not 4-byte aligned */
    CHECK(cpu.trap == TRAP_BRANCH_MISALIGNED && cpu.trap_value == BASE + 2u);

    RUN(JAL(ZERO, -4));                /* jumps below the memory base */
    CHECK(cpu.state == CPU_TRAPPED && cpu.trap == TRAP_FETCH_FAULT);

    /* executing the zero-filled tail of memory is an illegal instruction, not UB */
    RUN(ADDI(A0, ZERO, 1));
    CHECK(cpu.trap == TRAP_ILLEGAL_INSTR && cpu.pc == BASE + 4u);
}

static void test_syscalls(void)
{
    RUN(ADDI(A7, ZERO, 64), ADDI(A0, ZERO, 99), ECALL, ADDI(A3, A0, 0),          /* bad fd     */
        ADDI(A7, ZERO, 1000), ECALL, ADDI(A4, A0, 0),                            /* unknown    */
        ADDI(A7, ZERO, 64), ADDI(A0, ZERO, 1), ADDI(A1, ZERO, 0), ADDI(A2, ZERO, 4),
        ECALL, ADDI(A5, A0, 0),                                                  /* bad buffer */
        HALT);
    CHECK(cpu.x[A3] == (uint32_t)-9);    /* -EBADF  */
    CHECK(cpu.x[A4] == (uint32_t)-38);   /* -ENOSYS */
    CHECK(cpu.x[A5] == (uint32_t)-14);   /* -EFAULT: the guest cannot make the host read OOB */
    CHECK(cpu.state == CPU_HALTED);
}

static void test_decode_immediates(void)
{
    int32_t boff[] = { -4096, -8, -2, 2, 8, 4094 };
    for (size_t i = 0; i < sizeof boff / sizeof boff[0]; i++) {
        instr_t in = decode(BEQ(T0, T1, boff[i]));
        CHECK(in.op == OP_BEQ && in.imm == (uint32_t)boff[i]);
    }
    int32_t joff[] = { -1048576, -2048, -2, 2, 2048, 1048574 };
    for (size_t i = 0; i < sizeof joff / sizeof joff[0]; i++) {
        instr_t in = decode(JAL(RA, joff[i]));
        CHECK(in.op == OP_JAL && in.imm == (uint32_t)joff[i]);
    }
    int32_t soff[] = { -2048, -1, 0, 31, 32, 2047 };
    for (size_t i = 0; i < sizeof soff / sizeof soff[0]; i++) {
        instr_t in = decode(SW(T1, T0, soff[i]));
        CHECK(in.op == OP_SW && in.imm == (uint32_t)soff[i]);
    }
    CHECK(decode(SRAI(A0, A1, 31)).op == OP_SRAI);
    CHECK(decode(SRAI(A0, A1, 31)).imm == 31);
    CHECK(decode(0x40001013u).op == OP_ILLEGAL);   /* slli with bad funct7 */
}

static void test_disasm(void)
{
    CHECK_STR(dis(0x00000013u, BASE), "nop");
    CHECK_STR(dis(ADDI(A0, ZERO, 5), BASE), "li a0, 5");
    CHECK_STR(dis(ADDI(A0, ZERO, -5), BASE), "li a0, -5");
    CHECK_STR(dis(ADDI(A0, A1, 0), BASE), "mv a0, a1");
    CHECK_STR(dis(ADD(A0, T0, T1), BASE), "add a0, t0, t1");
    CHECK_STR(dis(LW(A0, SP, -4), BASE), "lw a0, -4(sp)");
    CHECK_STR(dis(SW(A0, SP, 8), BASE), "sw a0, 8(sp)");
    CHECK_STR(dis(JALR(ZERO, RA, 0), BASE), "ret");
    CHECK_STR(dis(LUI(A0, 0x12345), BASE), "lui a0, 0x12345");
    CHECK_STR(dis(BEQ(T0, T1, -16), BASE + 0x10), "beq t0, t1, 0x80000000");
    CHECK_STR(dis(JAL(ZERO, 8), BASE), "j 0x80000008");
    CHECK_STR(dis(ECALL, BASE), "ecall");
}

/* ------------------------------------------------------------------- fuzzer */
static uint32_t rng = 0x2545F491u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void test_fuzz(void)
{
    static const uint32_t opcodes[] = { 0x37, 0x17, 0x6f, 0x67, 0x63, 0x03, 0x23, 0x13, 0x33, 0x0f, 0x73 };
    cpu_t c;
    unsigned long bad = 0;

    CHECK(cpu_init(&c, BASE, 0x4000) == 0);
    for (int it = 0; it < 300000; it++) {
        for (int r = 1; r < 32; r++)
            c.x[r] = (rnd() & 1) ? BASE + rnd() % c.mem.size : rnd();
        c.x[0] = 0;

        uint32_t pc  = BASE + (rnd() % (c.mem.size / 4)) * 4u;
        uint32_t raw = (rnd() & ~0x7fu) | opcodes[rnd() % (sizeof opcodes / sizeof opcodes[0])];
        mem_write(&c.mem, pc, 4, raw);

        c.pc = pc;
        c.state = CPU_RUNNING;
        c.trap = TRAP_NONE;
        cpu_step(&c);

        if (c.x[0] != 0)                                  bad++;  /* x0 must stay zero      */
        if (c.state == CPU_TRAPPED && c.pc != pc)         bad++;  /* traps must be precise  */
        if (c.state == CPU_RUNNING && (c.pc & 3u))        bad++;  /* pc stays word-aligned  */
    }
    CHECK(bad == 0);
    cpu_free(&c);
}

#define RUN_TEST(fn) do { printf("%-38s", #fn); int before_ = tests_failed; fn(); \
    puts(tests_failed == before_ ? "ok" : "FAILED"); } while (0)

int main(void)
{
    RUN_TEST(test_alu);
    RUN_TEST(test_logic_and_shifts);
    RUN_TEST(test_x0_hardwired);
    RUN_TEST(test_lui_auipc);
    RUN_TEST(test_branch_loop);
    RUN_TEST(test_branch_conditions);
    RUN_TEST(test_jal_jalr);
    RUN_TEST(test_load_store);
    RUN_TEST(test_faults_are_precise);
    RUN_TEST(test_misalignment_and_wraparound);
    RUN_TEST(test_illegal_and_control_traps);
    RUN_TEST(test_syscalls);
    RUN_TEST(test_decode_immediates);
    RUN_TEST(test_disasm);
    RUN_TEST(test_fuzz);

    cpu_free(&cpu);
    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
