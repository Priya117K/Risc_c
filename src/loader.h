/*
 * loader.h - Load a program image into guest memory.
 *
 * Supported formats:
 *   ELF32 (little-endian, EM_RISCV): PT_LOAD segments are copied (bss zeroed
 *                                    implicitly) and pc is set to e_entry.
 *   flat binary:                     loaded at the memory base, entry = base.
 *   hex text:                        one 32-bit hex word per token, '#' or '//'
 *                                    comments; loaded at base, entry = base.
 *
 * All writes are bounds-checked: an image that does not fit is rejected with
 * a message, never truncated or written out of range.
 */
#ifndef RISKV_LOADER_H
#define RISKV_LOADER_H

#include <stddef.h>
#include "cpu.h"

typedef enum { LOAD_AUTO, LOAD_BIN, LOAD_HEX, LOAD_ELF } load_format_t;

/* Returns 0 on success; on failure returns -1 and fills err. */
int load_program(cpu_t *c, const char *path, load_format_t fmt, char *err, size_t err_len);

#endif
