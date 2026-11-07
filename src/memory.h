/*
 * memory.h - Bounds-checked, little-endian main memory for the RISK-V simulator.
 *
 * Every access goes through mem_read / mem_write / the block helpers. Nothing
 * else in the simulator touches the backing store, so there is exactly one
 * place where "can this access escape the simulated address space?" is decided.
 */
#ifndef RISKV_MEMORY_H
#define RISKV_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MEM_OK = 0,
    MEM_ERR_OOB,        /* any byte of the access lies outside [base, base+size) */
    MEM_ERR_MISALIGNED  /* address not a multiple of the access width (strict mode) */
} mem_status_t;

typedef struct {
    uint8_t *data;       /* host backing store (zero-initialised) */
    uint32_t base;       /* first valid guest address */
    uint32_t size;       /* bytes of guest memory */
    bool     strict_align;
} memory_t;

/* Returns 0 on success, -1 on invalid geometry or allocation failure. */
int  mem_init(memory_t *m, uint32_t base, uint32_t size);
void mem_free(memory_t *m);

/* True iff all of [addr, addr+len) is inside guest memory (overflow-safe). */
bool mem_in_range(const memory_t *m, uint32_t addr, uint64_t len);

/* width must be 1, 2 or 4. */
mem_status_t mem_read(const memory_t *m, uint32_t addr, unsigned width, uint32_t *out);
mem_status_t mem_write(memory_t *m, uint32_t addr, unsigned width, uint32_t value);

mem_status_t mem_load_block(memory_t *m, uint32_t addr, const void *src, size_t len);
mem_status_t mem_read_block(const memory_t *m, uint32_t addr, void *dst, size_t len);

#endif
