

#include <stdint.h>

typedef enum {
    FMT_R, FMT_I, FMT_SHIFT, FMT_LOAD, FMT_S, FMT_B, FMT_U, FMT_J, FMT_JALR, FMT_NONE
} fmt_t;

typedef enum {
    CLS_ALU, CLS_BRANCH, CLS_LOAD, CLS_STORE, CLS_JUMP, CLS_SYSTEM, CLS_COUNT
} iclass_t;

/*        id      mnemonic   format      class      */
#define OP_TABLE(X) \
    X(LUI,    "lui",    FMT_U,     CLS_ALU)    \
    X(AUIPC,  "auipc",  FMT_U,     CLS_ALU)    \
    X(JAL,    "jal",    FMT_J,     CLS_JUMP)   \
    X(JALR,   "jalr",   FMT_JALR,  CLS_JUMP)   \
    X(BEQ,    "beq",    FMT_B,     CLS_BRANCH) \
    X(BNE,    "bne",    FMT_B,     CLS_BRANCH) \
    X(BLT,    "blt",    FMT_B,     CLS_BRANCH) \
    X(BGE,    "bge",    FMT_B,     CLS_BRANCH) \
    X(BLTU,   "bltu",   FMT_B,     CLS_BRANCH) \
    X(BGEU,   "bgeu",   FMT_B,     CLS_BRANCH) \
    X(LB,     "lb",     FMT_LOAD,  CLS_LOAD)   \
    X(LH,     "lh",     FMT_LOAD,  CLS_LOAD)   \
    X(LW,     "lw",     FMT_LOAD,  CLS_LOAD)   \
    X(LBU,    "lbu",    FMT_LOAD,  CLS_LOAD)   \
    X(LHU,    "lhu",    FMT_LOAD,  CLS_LOAD)   \
    X(SB,     "sb",     FMT_S,     CLS_STORE)  \
    X(SH,     "sh",     FMT_S,     CLS_STORE)  \
    X(SW,     "sw",     FMT_S,     CLS_STORE)  \
    X(ADDI,   "addi",   FMT_I,     CLS_ALU)    \
    X(SLTI,   "slti",   FMT_I,     CLS_ALU)    \
    X(SLTIU,  "sltiu",  FMT_I,     CLS_ALU)    \
    X(XORI,   "xori",   FMT_I,     CLS_ALU)    \
    X(ORI,    "ori",    FMT_I,     CLS_ALU)    \
    X(ANDI,   "andi",   FMT_I,     CLS_ALU)    \
    X(MUL,    "mul",    FMT_R,     CLS_ALU)    \
    X(MULH,   "mulh",   FMT_R,     CLS_ALU)    \
    X(MULHSU, "mulhsu", FMT_R,     CLS_ALU)    \
    X(MULHU,  "mulhu",  FMT_R,     CLS_ALU)    \
    X(DIV,    "div",    FMT_R,     CLS_ALU)    \
    X(DIVU,   "divu",   FMT_R,     CLS_ALU)    \
    X(REM,    "rem",    FMT_R,     CLS_ALU)    \
    X(REMU,   "remu",   FMT_R,     CLS_ALU)    \
    X(SLLI,   "slli",   FMT_SHIFT, CLS_ALU)    \
    X(SRLI,   "srli",   FMT_SHIFT, CLS_ALU)    \
    X(SRAI,   "srai",   FMT_SHIFT, CLS_ALU)    \
    X(ADD,    "add",    FMT_R,     CLS_ALU)    \
    X(SUB,    "sub",    FMT_R,     CLS_ALU)    \
    X(SLL,    "sll",    FMT_R,     CLS_ALU)    \
    X(SLT,    "slt",    FMT_R,     CLS_ALU)    \
    X(SLTU,   "sltu",   FMT_R,     CLS_ALU)    \
    X(XOR,    "xor",    FMT_R,     CLS_ALU)    \
    X(SRL,    "srl",    FMT_R,     CLS_ALU)    \
    X(SRA,    "sra",    FMT_R,     CLS_ALU)    \
    X(OR,     "or",     FMT_R,     CLS_ALU)    \
    X(AND,    "and",    FMT_R,     CLS_ALU)    \
    X(FENCE,  "fence",  FMT_NONE,  CLS_SYSTEM) \
    X(ECALL,  "ecall",  FMT_NONE,  CLS_SYSTEM) \
    X(EBREAK, "ebreak", FMT_NONE,  CLS_SYSTEM) \
    X(ILLEGAL,"illegal",FMT_NONE,  CLS_SYSTEM)

typedef enum {
#define X(id, name, fmt, cls) OP_##id,
    OP_TABLE(X)
#undef X
    OP_COUNT
} op_t;

typedef struct {
    const char *name;
    fmt_t       fmt;
    iclass_t    cls;
} op_info_t;

extern const op_info_t op_info[OP_COUNT];

typedef struct {
    uint32_t raw;
    op_t     op;
    uint8_t  rd, rs1, rs2;
    uint32_t imm;   /* sign-extended, two's complement (shamt for shift-imm) */
} instr_t;

instr_t decode(uint32_t raw);

/* Sign-extend the low `bits` bits of v (1 <= bits <= 31). Portable C99. */
uint32_t sext32(uint32_t v, unsigned bits);

/* Reinterpret a uint32_t as int32_t without relying on implementation-defined casts. */
static inline int32_t s32(uint32_t v)
{
    return (v & 0x80000000u) ? -(int32_t)(~v) - 1 : (int32_t)v;
}

/* ABI register name ("zero", "ra", "sp", "a0", ...). r is masked to 0..31. */
const char *reg_name(unsigned r);

#endif
