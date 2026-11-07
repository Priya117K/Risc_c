/*
 * disasm.h - Turn a decoded instruction into assembly text (with common
 * pseudo-instructions: nop, mv, li, ret, j). Used by the tracer and debugger.
 */
#ifndef RISKV_DISASM_H
#define RISKV_DISASM_H

#include <stddef.h>
#include <stdint.h>
#include "decode.h"

void disasm(const instr_t *in, uint32_t pc, char *buf, size_t n);

#endif
