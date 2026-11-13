#include "cpu.h"

#include <stdbool.h>
#include <string.h>
#include "disasm.h"

int cpu_init(cpu_t *c, uint32_t base, uint32_t size)
{
    memset(c, 0, sizeof *c);
    if (mem_init(&c->mem, base, size) != 0)
        return -1;
    c->pc = base;
    c->x[2] = (uint32_t)((uint64_t)base + size) & ~15u;   /* sp = top of RAM */
    c->state = CPU_RUNNING;
    c->trap = TRAP_NONE;
    return 0;
}

void cpu_free(cpu_t *c)
{
    mem_free(&c->mem);
}

const char *trap_name(trap_t t)
{
    switch (t) {
    case TRAP_NONE:             return "none";
    case TRAP_FETCH_MISALIGNED: return "instruction address misaligned";
    case TRAP_FETCH_FAULT:      return "instruction access fault";
    case TRAP_ILLEGAL_INSTR:    return "illegal instruction";
    case TRAP_BRANCH_MISALIGNED:return "branch/jump target misaligned";
    case TRAP_LOAD_MISALIGNED:  return "load address misaligned";
    case TRAP_LOAD_FAULT:       return "load access fault";
    case TRAP_STORE_MISALIGNED: return "store address misaligned";
    case TRAP_STORE_FAULT:      return "store access fault";
    case TRAP_EBREAK:           return "breakpoint (ebreak)";
    }
    return "unknown";
}

static void raise_trap(cpu_t *c, trap_t t, uint32_t value)
{
    c->state = CPU_TRAPPED;
    c->trap = t;
    c->trap_value = value;
}

/* Arithmetic right shift without relying on implementation-defined behaviour. */
static uint32_t sra32(uint32_t v, unsigned sh)
{
    if (sh == 0)
        return v;
    uint32_t r = v >> sh;
    if (v & 0x80000000u)
        r |= ~(0xFFFFFFFFu >> sh);
    return r;
}

/* ---- RV32M helpers ------------------------------------------------------
 * The high-half multiplies compute the full 64-bit product, convert to
 * uint64_t (well-defined modular conversion), then take the upper 32 bits
 * with a logical shift, so no implementation-defined signed shift is used.
 */
static uint32_t mulh_ss(uint32_t a, uint32_t b)      /* MULH   */
{
    int64_t p = (int64_t)s32(a) * (int64_t)s32(b);
    return (uint32_t)((uint64_t)p >> 32);
}

static uint32_t mulh_su(uint32_t a, uint32_t b)      /* MULHSU */
{
    int64_t p = (int64_t)s32(a) * (int64_t)b;        /* b treated as unsigned */
    return (uint32_t)((uint64_t)p >> 32);
}

static uint32_t mulh_uu(uint32_t a, uint32_t b)      /* MULHU  */
{
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}

/* Division never traps in RISC-V:
 *   x / 0        -> all ones (DIV, DIVU);  x % 0 -> x (REM, REMU)
 *   INT_MIN / -1 -> INT_MIN (DIV);         INT_MIN % -1 -> 0 (REM)
 */
static uint32_t div_s(uint32_t a, uint32_t b)        /* DIV  */
{
    if (b == 0)
        return 0xFFFFFFFFu;
    if (a == 0x80000000u && b == 0xFFFFFFFFu)
        return a;
    return (uint32_t)(s32(a) / s32(b));
}

static uint32_t div_u(uint32_t a, uint32_t b)        /* DIVU */
{
    return b == 0 ? 0xFFFFFFFFu : a / b;
}

static uint32_t rem_s(uint32_t a, uint32_t b)        /* REM  */
{
    if (b == 0)
        return a;
    if (a == 0x80000000u && b == 0xFFFFFFFFu)
        return 0;
    return (uint32_t)(s32(a) % s32(b));
}

static uint32_t rem_u(uint32_t a, uint32_t b)        /* REMU */
{
    return b == 0 ? a : a % b;
}

static bool branch_taken(op_t op, uint32_t a, uint32_t b)
{
    switch (op) {
    case OP_BEQ:  return a == b;
    case OP_BNE:  return a != b;
    case OP_BLT:  return s32(a) <  s32(b);
    case OP_BGE:  return s32(a) >= s32(b);
    case OP_BLTU: return a <  b;
    case OP_BGEU: return a >= b;
    default:      return false;
    }
}

static void do_ecall(cpu_t *c)
{
    switch (c->x[17]) {
    case SYS_EXIT:
        c->exit_code = s32(c->x[10]);
        c->state = CPU_HALTED;
        break;

    case SYS_WRITE: {
        uint32_t fd = c->x[10], buf = c->x[11], len = c->x[12];
        FILE *f = (fd == 1) ? stdout : (fd == 2) ? stderr : NULL;
        if (!f) {
            c->x[10] = (uint32_t)-9;            /* -EBADF  */
            break;
        }
        if (!mem_in_range(&c->mem, buf, len)) {
            c->x[10] = (uint32_t)-14;           /* -EFAULT */
            break;
        }
        uint8_t chunk[256];
        uint32_t done = 0;
        while (done < len) {
            uint32_t n = len - done;
            if (n > sizeof chunk)
                n = (uint32_t)sizeof chunk;
            mem_read_block(&c->mem, buf + done, chunk, n);
            fwrite(chunk, 1, n, f);
            done += n;
        }
        fflush(f);
        c->x[10] = len;
        break;
    }

    default:
        c->x[10] = (uint32_t)-38;               /* -ENOSYS */
        break;
    }
}

void cpu_step(cpu_t *c)
{
    if (c->state != CPU_RUNNING)
        return;

    /* ---- FETCH ---------------------------------------------------------- */
    uint32_t raw;
    if (c->pc & 3u) {
        raise_trap(c, TRAP_FETCH_MISALIGNED, c->pc);
        return;
    }
    if (mem_read(&c->mem, c->pc, 4, &raw) != MEM_OK) {
        raise_trap(c, TRAP_FETCH_FAULT, c->pc);
        return;
    }

    /* ---- DECODE --------------------------------------------------------- */
    instr_t in = decode(raw);

    if (c->trace) {
        char text[64];
        disasm(&in, c->pc, text, sizeof text);
        fprintf(c->trace, "[%8llu] 0x%08x: %08x  %s\n",
                (unsigned long long)c->instret, (unsigned)c->pc, (unsigned)raw, text);
    }

    /* ---- EXECUTE -------------------------------------------------------- */
    const uint32_t a = c->x[in.rs1];
    const uint32_t b = c->x[in.rs2];
    const uint32_t imm = in.imm;
    const uint32_t y = (op_info[in.op].fmt == FMT_R) ? b : imm;  /* ALU 2nd operand */
    uint32_t next = c->pc + 4u;
    uint32_t res = 0;
    bool wb = false;

    switch (in.op) {
    case OP_LUI:   res = imm;          wb = true; break;
    case OP_AUIPC: res = c->pc + imm;  wb = true; break;

    case OP_JAL: {
        uint32_t t = c->pc + imm;
        if (t & 3u) { raise_trap(c, TRAP_BRANCH_MISALIGNED, t); return; }
        res = next; wb = true; next = t;
        break;
    }
    case OP_JALR: {
        uint32_t t = (a + imm) & ~1u;
        if (t & 3u) { raise_trap(c, TRAP_BRANCH_MISALIGNED, t); return; }
        res = next; wb = true; next = t;
        break;
    }

    case OP_BEQ: case OP_BNE: case OP_BLT:
    case OP_BGE: case OP_BLTU: case OP_BGEU:
        if (branch_taken(in.op, a, b)) {
            uint32_t t = c->pc + imm;
            if (t & 3u) { raise_trap(c, TRAP_BRANCH_MISALIGNED, t); return; }
            next = t;
            c->branches_taken++;
        }
        break;

    case OP_LB: case OP_LH: case OP_LW: case OP_LBU: case OP_LHU: {
        uint32_t addr = a + imm, v;
        unsigned w = (in.op == OP_LB || in.op == OP_LBU) ? 1u :
                     (in.op == OP_LH || in.op == OP_LHU) ? 2u : 4u;
        mem_status_t s = mem_read(&c->mem, addr, w, &v);
        if (s != MEM_OK) {
            raise_trap(c, s == MEM_ERR_MISALIGNED ? TRAP_LOAD_MISALIGNED : TRAP_LOAD_FAULT, addr);
            return;
        }
        if (in.op == OP_LB) v = sext32(v, 8);
        if (in.op == OP_LH) v = sext32(v, 16);
        res = v; wb = true;
        c->loads_bytes += w;
        break;
    }

    case OP_SB: case OP_SH: case OP_SW: {
        uint32_t addr = a + imm;
        unsigned w = (in.op == OP_SB) ? 1u : (in.op == OP_SH) ? 2u : 4u;
        mem_status_t s = mem_write(&c->mem, addr, w, b);
        if (s != MEM_OK) {
            raise_trap(c, s == MEM_ERR_MISALIGNED ? TRAP_STORE_MISALIGNED : TRAP_STORE_FAULT, addr);
            return;
        }
        c->stores_bytes += w;
        break;
    }

    case OP_ADD:  case OP_ADDI:  res = a + y;                         wb = true; break;
    case OP_SUB:                 res = a - y;                         wb = true; break;
    case OP_SLL:  case OP_SLLI:  res = a << (y & 31u);                wb = true; break;
    case OP_SLT:  case OP_SLTI:  res = s32(a) < s32(y) ? 1u : 0u;     wb = true; break;
    case OP_SLTU: case OP_SLTIU: res = a < y ? 1u : 0u;               wb = true; break;
    case OP_XOR:  case OP_XORI:  res = a ^ y;                         wb = true; break;
    case OP_SRL:  case OP_SRLI:  res = a >> (y & 31u);                wb = true; break;
    case OP_SRA:  case OP_SRAI:  res = sra32(a, y & 31u);             wb = true; break;
    case OP_OR:   case OP_ORI:   res = a | y;                         wb = true; break;
    case OP_AND:  case OP_ANDI:  res = a & y;                         wb = true; break;

    /* ---- RV32M: multiply / divide (R-type, funct7 = 0x01) ---- */
    case OP_MUL:    res = a * b;            wb = true; break;
    case OP_MULH:   res = mulh_ss(a, b);    wb = true; break;
    case OP_MULHSU: res = mulh_su(a, b);    wb = true; break;
    case OP_MULHU:  res = mulh_uu(a, b);    wb = true; break;
    case OP_DIV:    res = div_s(a, b);      wb = true; break;
    case OP_DIVU:   res = div_u(a, b);      wb = true; break;
    case OP_REM:    res = rem_s(a, b);      wb = true; break;
    case OP_REMU:   res = rem_u(a, b);      wb = true; break;

    case OP_FENCE:  /* single hart, in-order: architectural no-op */   break;
    case OP_ECALL:  do_ecall(c);                                       break;
    case OP_EBREAK: raise_trap(c, TRAP_EBREAK, c->pc);                 return;

    case OP_ILLEGAL:
    default:
        raise_trap(c, TRAP_ILLEGAL_INSTR, raw);
        return;
    }

    /* ---- WRITE-BACK / COMMIT (only reached if no trap was raised) -------- */
    if (wb && in.rd != 0)
        c->x[in.rd] = res;
    c->x[0] = 0;
    c->pc = next;
    c->instret++;
    c->class_count[op_info[in.op].cls]++;
}

void cpu_run(cpu_t *c, uint64_t max_instr)
{
    while (c->state == CPU_RUNNING && (max_instr == 0 || c->instret < max_instr))
        cpu_step(c);
}

void cpu_dump_regs(FILE *f, const cpu_t *c)
{
    fprintf(f, "pc   0x%08x\n", (unsigned)c->pc);
    for (unsigned i = 0; i < 32; i++) {
        fprintf(f, "x%-2u %-4s 0x%08x%s", i, reg_name(i), (unsigned)c->x[i],
                (i % 4 == 3) ? "\n" : "   ");
    }
}

void cpu_report(FILE *f, const cpu_t *c)
{
    unsigned long long n = (unsigned long long)c->instret;
    switch (c->state) {
    case CPU_HALTED:
        fprintf(f, "[riskv] program exited with code %d after %llu instructions\n",
                c->exit_code, n);
        break;
    case CPU_TRAPPED: {
        uint32_t raw;
        fprintf(f, "[riskv] TRAP: %s at pc=0x%08x (value=0x%08x) after %llu instructions\n",
                trap_name(c->trap), (unsigned)c->pc, (unsigned)c->trap_value, n);
        if (mem_read(&c->mem, c->pc, 4, &raw) == MEM_OK) {
            char text[64];
            instr_t in = decode(raw);
            disasm(&in, c->pc, text, sizeof text);
            fprintf(f, "[riskv]   faulting instruction: %08x  %s\n", (unsigned)raw, text);
        }
        break;
    }
    case CPU_RUNNING:
        fprintf(f, "[riskv] stopped while running after %llu instructions\n", n);
        break;
    }
}

void cpu_print_stats(FILE *f, const cpu_t *c)
{
    static const char *const names[CLS_COUNT] = {
        "alu", "branch", "load", "store", "jump", "system"
    };
    unsigned long long total = (unsigned long long)c->instret;
    fprintf(f, "---- statistics ----\n");
    fprintf(f, "instructions retired : %llu\n", total);
    for (int i = 0; i < CLS_COUNT; i++) {
        unsigned long long n = (unsigned long long)c->class_count[i];
        fprintf(f, "  %-8s %12llu  (%5.1f%%)\n", names[i], n,
                total ? 100.0 * (double)n / (double)total : 0.0);
    }
   
}
