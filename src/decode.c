#include "decode.h"

const op_info_t op_info[OP_COUNT] = {
#define X(id, name, fmt, cls) { name, fmt, cls },
    OP_TABLE(X)
#undef X
};

static const char *const abi_names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10","s11","t3", "t4", "t5", "t6"
};

const char *reg_name(unsigned r)
{
    return abi_names[r & 31u];
}

uint32_t sext32(uint32_t v, unsigned bits)
{
    uint32_t m = 1u << (bits - 1u);
    v &= (m << 1) - 1u;
    return (v ^ m) - m;
}

instr_t decode(uint32_t raw)
{
    instr_t in;
    op_t op = OP_ILLEGAL;

    unsigned opcode = raw & 0x7fu;
    unsigned f3     = (raw >> 12) & 7u;
    unsigned f7     = raw >> 25;

    in.raw = raw;
    in.rd  = (uint8_t)((raw >> 7)  & 0x1fu);
    in.rs1 = (uint8_t)((raw >> 15) & 0x1fu);
    in.rs2 = (uint8_t)((raw >> 20) & 0x1fu);
    in.imm = 0;

    switch (opcode) {
    case 0x37: op = OP_LUI;   in.imm = raw & 0xfffff000u; break;
    case 0x17: op = OP_AUIPC; in.imm = raw & 0xfffff000u; break;

    case 0x6f: /* JAL: imm[20|10:1|11|19:12] */
        op = OP_JAL;
        in.imm = sext32(((raw >> 31) << 20) |
                        (((raw >> 12) & 0xffu) << 12) |
                        (((raw >> 20) & 1u) << 11) |
                        (((raw >> 21) & 0x3ffu) << 1), 21);
        break;

    case 0x67:
        if (f3 == 0) op = OP_JALR;
        in.imm = sext32(raw >> 20, 12);
        break;

    case 0x63: /* branches: imm[12|10:5|4:1|11] */
        switch (f3) {
        case 0: op = OP_BEQ;  break;
        case 1: op = OP_BNE;  break;
        case 4: op = OP_BLT;  break;
        case 5: op = OP_BGE;  break;
        case 6: op = OP_BLTU; break;
        case 7: op = OP_BGEU; break;
        default: break;
        }
        in.imm = sext32(((raw >> 31) << 12) |
                        (((raw >> 7) & 1u) << 11) |
                        (((raw >> 25) & 0x3fu) << 5) |
                        (((raw >> 8) & 0xfu) << 1), 13);
        break;

    case 0x03:
        switch (f3) {
        case 0: op = OP_LB;  break;
        case 1: op = OP_LH;  break;
        case 2: op = OP_LW;  break;
        case 4: op = OP_LBU; break;
        case 5: op = OP_LHU; break;
        default: break;
        }
        in.imm = sext32(raw >> 20, 12);
        break;

    case 0x23:
        switch (f3) {
        case 0: op = OP_SB; break;
        case 1: op = OP_SH; break;
        case 2: op = OP_SW; break;
        default: break;
        }
        in.imm = sext32(((raw >> 25) << 5) | ((raw >> 7) & 0x1fu), 12);
        break;

    case 0x13:
        in.imm = sext32(raw >> 20, 12);
        switch (f3) {
        case 0: op = OP_ADDI;  break;
        case 2: op = OP_SLTI;  break;
        case 3: op = OP_SLTIU; break;
        case 4: op = OP_XORI;  break;
        case 6: op = OP_ORI;   break;
        case 7: op = OP_ANDI;  break;
        case 1:
            if (f7 == 0x00) { op = OP_SLLI; in.imm = in.rs2; }
            break;
        case 5:
            if (f7 == 0x00)      { op = OP_SRLI; in.imm = in.rs2; }
            else if (f7 == 0x20) { op = OP_SRAI; in.imm = in.rs2; }
            break;
        default: break;
        }
        break;

    case 0x33:
        if (f7 == 0x00) {
            switch (f3) {
            case 0: op = OP_ADD;  break;
            case 1: op = OP_SLL;  break;
            case 2: op = OP_SLT;  break;
            case 3: op = OP_SLTU; break;
            case 4: op = OP_XOR;  break;
            case 5: op = OP_SRL;  break;
            case 6: op = OP_OR;   break;
            case 7: op = OP_AND;  break;
            default: break;
            }
        } else if (f7 == 0x20) {
            if (f3 == 0)      op = OP_SUB;
            else if (f3 == 5) op = OP_SRA;
        }
        break;

    case 0x0f:
        if (f3 == 0) op = OP_FENCE;
        break;

    case 0x73:
        if (raw == 0x00000073u)      op = OP_ECALL;
        else if (raw == 0x00100073u) op = OP_EBREAK;
        break;

    default:
        break;
    }

    in.op = op;
    return in;
}
