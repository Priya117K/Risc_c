/*
 * debugger.h - Minimal gdb-style interactive front end (step, continue,
 * breakpoints, register and memory inspection).
 */
#ifndef RISKV_DEBUGGER_H
#define RISKV_DEBUGGER_H

#include "cpu.h"

void debugger_run(cpu_t *c);

#endif

