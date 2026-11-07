#include "loader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_BYTES (256ul * 1024ul * 1024ul)
#define EM_RISCV 243u
#define PT_LOAD  1u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Reads the whole file; buffer is NUL-terminated (so it doubles as text). */
static uint8_t *read_file(const char *path, size_t *len, char *err, size_t en)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, en, "cannot open '%s'", path);
        return NULL;
    }
    long sz = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > MAX_FILE_BYTES || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        snprintf(err, en, "'%s': unsupported file size", path);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        snprintf(err, en, "out of memory reading '%s'", path);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        snprintf(err, en, "short read on '%s'", path);
        return NULL;
    }
    buf[sz] = 0;
    *len = (size_t)sz;
    return buf;
}

static int load_elf(cpu_t *c, const uint8_t *b, size_t len, char *err, size_t en)
{
    if (len < 52 || memcmp(b, "\x7f" "ELF", 4) != 0) {
        snprintf(err, en, "not an ELF file");
        return -1;
    }
    if (b[4] != 1) { snprintf(err, en, "ELF is not 32-bit"); return -1; }
    if (b[5] != 1) { snprintf(err, en, "ELF is not little-endian"); return -1; }
    if (rd16(b + 18) != EM_RISCV) {
        snprintf(err, en, "ELF is not RISC-V (e_machine=%u)", (unsigned)rd16(b + 18));
        return -1;
    }

    uint32_t entry = rd32(b + 24);
    uint32_t phoff = rd32(b + 28);
    unsigned phentsize = rd16(b + 42);
    unsigned phnum = rd16(b + 44);
    if (phnum == 0 || phentsize < 32) {
        snprintf(err, en, "ELF has no usable program headers");
        return -1;
    }

    unsigned loaded = 0;
    for (unsigned i = 0; i < phnum; i++) {
        uint64_t off = (uint64_t)phoff + (uint64_t)i * phentsize;
        if (off + 32 > len) {
            snprintf(err, en, "truncated ELF program header table");
            return -1;
        }
        const uint8_t *ph = b + off;
        if (rd32(ph) != PT_LOAD)
            continue;
        uint32_t p_offset = rd32(ph + 4);
        uint32_t vaddr    = rd32(ph + 8);
        uint32_t filesz   = rd32(ph + 16);
        uint32_t memsz    = rd32(ph + 20);

        if (filesz > memsz || (uint64_t)p_offset + filesz > len) {
            snprintf(err, en, "segment %u has inconsistent sizes", i);
            return -1;
        }
        if (!mem_in_range(&c->mem, vaddr, memsz)) {
            snprintf(err, en, "segment %u [0x%08x, +0x%x) lies outside simulated memory "
                     "(use -b / -m to adjust)", i, (unsigned)vaddr, (unsigned)memsz);
            return -1;
        }
        mem_load_block(&c->mem, vaddr, b + p_offset, filesz);
        loaded++;
    }
    if (!loaded) {
        snprintf(err, en, "ELF has no PT_LOAD segments");
        return -1;
    }
    c->pc = entry;
    return 0;
}

static int load_bin(cpu_t *c, const uint8_t *b, size_t len, char *err, size_t en)
{
    if (len == 0) {
        snprintf(err, en, "program file is empty");
        return -1;
    }
    if (mem_load_block(&c->mem, c->mem.base, b, len) != MEM_OK) {
        snprintf(err, en, "binary (%lu bytes) does not fit in simulated memory",
                 (unsigned long)len);
        return -1;
    }
    c->pc = c->mem.base;
    return 0;
}

static int load_hex(cpu_t *c, const char *text, char *err, size_t en)
{
    uint32_t addr = c->mem.base;
    unsigned line = 1;
    size_t words = 0;
    const char *p = text;

    while (*p) {
        if (*p == '\n') { line++; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (*p == '#' || (p[0] == '/' && p[1] == '/')) {
            while (*p && *p != '\n') p++;
            continue;
        }
        char *end;
        unsigned long v = strtoul(p, &end, 16);
        if (end == p || v > 0xFFFFFFFFul ||
            (*end && !isspace((unsigned char)*end) && *end != '#' && *end != '/')) {
            snprintf(err, en, "hex file line %u: invalid 32-bit word", line);
            return -1;
        }
        if (mem_write(&c->mem, addr, 4, (uint32_t)v) != MEM_OK) {
            snprintf(err, en, "hex file line %u: cannot place word at 0x%08x "
                     "(program too large for memory?)", line, (unsigned)addr);
            return -1;
        }
        addr += 4;
        words++;
        p = end;
    }
    if (words == 0) {
        snprintf(err, en, "hex file contains no instructions");
        return -1;
    }
    c->pc = c->mem.base;
    return 0;
}

int load_program(cpu_t *c, const char *path, load_format_t fmt, char *err, size_t err_len)
{
    size_t len = 0;
    uint8_t *buf = read_file(path, &len, err, err_len);
    if (!buf)
        return -1;

    if (fmt == LOAD_AUTO) {
        size_t pl = strlen(path);
        if (len >= 4 && memcmp(buf, "\x7f" "ELF", 4) == 0)
            fmt = LOAD_ELF;
        else if (pl >= 4 && strcmp(path + pl - 4, ".hex") == 0)
            fmt = LOAD_HEX;
        else
            fmt = LOAD_BIN;
    }

    int rc;
    switch (fmt) {
    case LOAD_ELF: rc = load_elf(c, buf, len, err, err_len); break;
    case LOAD_HEX: rc = load_hex(c, (const char *)buf, err, err_len); break;
    default:       rc = load_bin(c, buf, len, err, err_len); break;
    }
    free(buf);
    return rc;
}
