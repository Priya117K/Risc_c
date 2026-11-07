#include "memory.h"

#include <stdlib.h>
#include <string.h>

int mem_init(memory_t *m, uint32_t base, uint32_t size)
{
    m->data = NULL;
    m->base = 0;
    m->size = 0;
    m->strict_align = true;

    /* Guest space is 32-bit: the region must not wrap past 2^32. */
    if (size < 4 || (uint64_t)base + size > 0x100000000ull)
        return -1;

    m->data = calloc(size, 1);
    if (!m->data)
        return -1;
    m->base = base;
    m->size = size;
    return 0;
}

void mem_free(memory_t *m)
{
    free(m->data);
    m->data = NULL;
    m->size = 0;
}

bool mem_in_range(const memory_t *m, uint32_t addr, uint64_t len)
{
    if (!m->data || addr < m->base)
        return false;
    uint64_t off = (uint64_t)(addr - m->base);
    /* Written as len <= size - off so that addr + len can never overflow. */
    return off <= m->size && len <= (uint64_t)m->size - off;
}

mem_status_t mem_read(const memory_t *m, uint32_t addr, unsigned width, uint32_t *out)
{
    if (width != 1 && width != 2 && width != 4)
        return MEM_ERR_OOB;
    if (m->strict_align && (addr & (width - 1u)))
        return MEM_ERR_MISALIGNED;
    if (!mem_in_range(m, addr, width))
        return MEM_ERR_OOB;

    const uint8_t *p = m->data + (addr - m->base);
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++)
        v |= (uint32_t)p[i] << (8u * i);   /* little-endian, host independent */
    *out = v;
    return MEM_OK;
}

mem_status_t mem_write(memory_t *m, uint32_t addr, unsigned width, uint32_t value)
{
    if (width != 1 && width != 2 && width != 4)
        return MEM_ERR_OOB;
    if (m->strict_align && (addr & (width - 1u)))
        return MEM_ERR_MISALIGNED;
    if (!mem_in_range(m, addr, width))
        return MEM_ERR_OOB;

    uint8_t *p = m->data + (addr - m->base);
    for (unsigned i = 0; i < width; i++)
        p[i] = (uint8_t)(value >> (8u * i));
    return MEM_OK;
}

mem_status_t mem_load_block(memory_t *m, uint32_t addr, const void *src, size_t len)
{
    if (!mem_in_range(m, addr, len))
        return MEM_ERR_OOB;
    if (len)
        memcpy(m->data + (addr - m->base), src, len);
    return MEM_OK;
}

mem_status_t mem_read_block(const memory_t *m, uint32_t addr, void *dst, size_t len)
{
    if (!mem_in_range(m, addr, len))
        return MEM_ERR_OOB;
    if (len)
        memcpy(dst, m->data + (addr - m->base), len);
    return MEM_OK;
}
