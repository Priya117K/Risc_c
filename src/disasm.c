#include "disasm.h"

#include <stdio.h>

void disasm(const instr_t *in, uint32_t pc, char *buf, size_t n)
{
    const op_info_t *oi = &op_info[in->op];
    const char *rd  = reg_name(in->rd);
    const char *rs1 = reg_name(in->rs1);
    const char *rs2 = reg_name(in->rs2);
    int32_t imm = s32(in->imm);
    uint32_t target;

    switch (oi->fmt) {
    case FMT_R:
        snprintf(buf, n, "%s %s, %s, %s", oi->name, rd, rs1, rs2);
        break;

    case FMT_I:
        if (in->op == OP_ADDI) {
            if (in->rd == 0 && in->rs1 == 0 && imm == 0) {
                snprintf(buf, n, "nop");
                break;
            }
            if (in->rs1 == 0) {
                snprintf(buf, n, "li %s, %d", rd, (int)imm);
                break;
            }
            if (imm == 0) {
                snprintf(buf, n, "mv %s, %s", rd, rs1);
                break;
            }
        }
        snprintf(buf, n, "%s %s, %s, %d", oi->name, rd, rs1, (int)imm);
        break;

    case FMT_SHIFT:
        snprintf(buf, n, "%s %s, %s, %u", oi->name, rd, rs1, (unsigned)in->imm);
        break;

    case FMT_LOAD:
        snprintf(buf, n, "%s %s, %d(%s)", oi->name, rd, (int)imm, rs1);
        break;

    case FMT_S:
        snprintf(buf, n, "%s %s, %d(%s)", oi->name, rs2, (int)imm, rs1);
        break;

    case FMT_B:
        target = pc + in->imm;
        snprintf(buf, n, "%s %s, %s, 0x%08x", oi->name, rs1, rs2, (unsigned)target);
        break;

    case FMT_U:
        snprintf(buf, n, "%s %s, 0x%x", oi->name, rd, (unsigned)(in->imm >> 12));
        break;

    case FMT_J:
        target = pc + in->imm;
        if (in->rd == 0)
            snprintf(buf, n, "j 0x%08x", (unsigned)target);
        else
            snprintf(buf, n, "jal %s, 0x%08x", rd, (unsigned)target);
        break;

    case FMT_JALR:
        if (in->rd == 0 && in->rs1 == 1 && imm == 0)
            snprintf(buf, n, "ret");
        else
            snprintf(buf, n, "jalr %s, %d(%s)", rd, (int)imm, rs1);
        break;

    case FMT_NONE:
    default:
        if (in->op == OP_ILLEGAL)
            snprintf(buf, n, ".word 0x%08x", (unsigned)in->raw);
        else
            snprintf(buf, n, "%s", oi->name);
        break;
    }
}
