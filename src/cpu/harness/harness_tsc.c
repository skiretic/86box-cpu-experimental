/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: what a case needs to measure guest time on
 *          any CPU and to place code where it chooses. The TSC read
 *          device; a ROM window; pages a case loads at physical addresses
 *          of its own choice and owns as it owns its code page; the
 *          measurements a case leaves in the record page.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "harness.h"

/* The TSC read device's IN: the time as RDTSC would read it, with the
   recompiler's block cycles so far counted first (opRDTSC). */
uint32_t
harness_tsc_read(void)
{
    if (cpu_use_dynarec)
        update_tsc();
    return (uint32_t) tsc;
}

/* ---- ROM window ------------------------------------------------------ */

int harness_rom;

static uint8_t       rom_buf[HARNESS_ROM_SIZE];
static mem_mapping_t rom_mapping;

static int
in_rom(uint32_t addr)
{
    return harness_rom && (addr >= HARNESS_ROM) && (addr < (HARNESS_ROM + HARNESS_ROM_SIZE));
}

static uint8_t
rom_read_b(uint32_t addr, UNUSED(void *priv))
{
    return rom_buf[addr & (HARNESS_ROM_SIZE - 1)];
}

static uint16_t
rom_read_w(uint32_t addr, UNUSED(void *priv))
{
    return rom_read_b(addr, priv) | (rom_read_b(addr + 1, priv) << 8);
}

static uint32_t
rom_read_l(uint32_t addr, UNUSED(void *priv))
{
    return rom_read_w(addr, priv) | ((uint32_t) rom_read_w(addr + 2, priv) << 16);
}

/* Once per process, after the machine's setup: the mappings are not a
   case's to change (restore_memory()). The window is external with no
   ROMCS# needed, which the memory state setup leaves the range in, and
   writes to it go nowhere. Fetches from it take cpu_rom_prefetch_cycles
   (getpccache()). */
void
harness_rom_setup(void)
{
    memset(rom_buf, 0xf4, sizeof(rom_buf));
    mem_mapping_add(&rom_mapping, HARNESS_ROM, HARNESS_ROM_SIZE, rom_read_b, rom_read_w, rom_read_l,
                    NULL, NULL, NULL, rom_buf, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM, NULL);
}

/* ---- extra pages ----------------------------------------------------- */

/* The case whose pages are in RAM, and so owned, since its last load, and
   the addresses loaded then: the case may be overwritten in place by the
   next one before that is loaded. */
static const harness_case_t *pages_case;
static uint32_t              loaded[HARNESS_PAGES_MAX];
static int                   loaded_nr;

/* The "@<phys> <bytes>" parts of a case's hex text, after its body: each
   is one page, its bytes from the page's start and HLT past them, as the
   code page is filled. Returns the number of pages, or -1 with the reason
   printed. */
int
harness_pages_parse(const char *s, harness_case_t *c)
{
    c->pages_nr = 0;
    s = strchr(s, '@');
    while (s) {
        harness_page_t *p;
        char           *end;
        unsigned long   phys;
        int             n = 0;

        if (c->pages_nr == HARNESS_PAGES_MAX) {
            fprintf(stderr, "a case has at most %d extra pages\n", HARNESS_PAGES_MAX);
            return -1;
        }
        phys = strtoul(s + 1, &end, 16);
        if ((end == s + 1) || (phys & (HARNESS_PAGE - 1)) || (phys < HARNESS_PAGES_LOW) || (phys > 0xfffff000UL)) {
            fprintf(stderr, "an extra page is @<phys>, page aligned and from %x up\n", HARNESS_PAGES_LOW);
            return -1;
        }
        for (int i = 0; i < c->pages_nr; i++) {
            if (c->pages[i].phys == phys) {
                fprintf(stderr, "extra page %lx given twice\n", phys);
                return -1;
            }
        }
        p       = &c->pages[c->pages_nr++];
        p->phys = (uint32_t) phys;
        memset(p->bytes, 0xf4, sizeof(p->bytes));
        s = end;
        while (*s && (*s != '@')) {
            unsigned v;

            if ((*s == ' ') || (*s == ',') || (*s == '\n') || (*s == '\r') || (*s == '\t')) {
                s++;
                continue;
            }
            if ((n >= HARNESS_PAGE) || (sscanf(s, "%2x", &v) != 1)) {
                fprintf(stderr, "bad bytes for extra page %lx\n", phys);
                return -1;
            }
            p->bytes[n++] = v;
            s += 2;
        }
        if (!*s)
            s = NULL;
    }
    return c->pages_nr;
}

static int
case_has_page(const harness_case_t *c, uint32_t addr)
{
    for (int i = 0; c && (i < c->pages_nr); i++) {
        if (c->pages[i].phys == addr)
            return 1;
    }
    return 0;
}

/* The case's pages into RAM, as the code page is loaded, and the pages of
   the case before it that this one does not have put back as setup left
   them (ref), past the CPU, as every page a case does not own is. */
void
harness_pages_load(const harness_case_t *c, const uint8_t *ref)
{
    for (int i = 0; i < loaded_nr; i++) {
        const uint32_t a = loaded[i];

        if (case_has_page(c, a) || in_rom(a))
            continue;
        memcpy(ram + a, ref + a, HARNESS_PAGE);
        mem_invalidate_range(a, a);
    }
    pages_case = c;
    loaded_nr  = 0;
    if (harness_rom)
        memset(rom_buf, 0xf4, sizeof(rom_buf));
    for (int i = 0; i < c->pages_nr; i++) {
        if (in_rom(c->pages[i].phys)) {
            memcpy(rom_buf + (c->pages[i].phys - HARNESS_ROM), c->pages[i].bytes, HARNESS_PAGE);
            continue;
        }
        if (c->pages[i].phys >= mem_size * 1024)
            harness_fatal("an extra page is past the end of RAM");
        memcpy(ram + c->pages[i].phys, c->pages[i].bytes, HARNESS_PAGE);
        loaded[loaded_nr++] = c->pages[i].phys;
    }
}

int
harness_pages_own(uint32_t addr)
{
    return case_has_page(pages_case, addr);
}

/* As capture_code_page() does for the code page: bytes a run left changed
   in an extra page are counted and hashed with the code page's, and written
   back as a CPU write would write them. */
void
harness_pages_capture(uint32_t *bytes, uint32_t *first, uint64_t *hash)
{
    for (int p = 0; pages_case && (p < pages_case->pages_nr); p++) {
        const uint32_t  base  = pages_case->pages[p].phys;
        const uint8_t  *image = pages_case->pages[p].bytes;

        if (in_rom(base) || !memcmp(ram + base, image, HARNESS_PAGE))
            continue;
        *hash = (*hash ^ base) * 0x100000001b3ull;
        for (int i = 0; i < HARNESS_PAGE; i++)
            *hash = (*hash ^ ram[base + i]) * 0x100000001b3ull;
        for (int i = 0; i < HARNESS_PAGE; i++) {
            const uint32_t a = base + i;

            if (ram[a] == image[i])
                continue;
            if (!*bytes)
                *first = a;
            (*bytes)++;
            mem_write_ramb_page(a, image[i], &pages[a >> 12]);
        }
    }
}

/* ---- measurements ---------------------------------------------------- */

void
harness_meas_capture(uint32_t *meas)
{
    for (int i = 0; i <= REC_MEAS_NR; i++) {
        const uint32_t a = HARNESS_RECORD + REC_MEAS + i * 4;

        meas[i] = ram[a] | (ram[a + 1] << 8) | (ram[a + 2] << 16) | ((uint32_t) ram[a + 3] << 24);
    }
}

/* One line per run of a case that wrote the magic, with the run named as
   the kit's expectation files name it (interp, mark, compile, compile2,
   ..., compiled, compiled2). */
void
harness_meas_print(const uint32_t *meas, const char *run)
{
    char token[24];
    int  n = 0;

    if (meas[0] != REC_MEAS_MAGIC)
        return;
    if (!strcmp(run, "interpreter"))
        strcpy(token, "interp");
    else if (!strcmp(run, "mark pass"))
        strcpy(token, "mark");
    else {
        for (const char *p = run; *p && (n < (int) sizeof(token) - 1); p++) {
            if (!strncmp(p, " pass", 5))
                p += 4;
            else if (*p != ' ')
                token[n++] = *p;
        }
        token[n] = 0;
    }
    printf("RUNSTATE %s", token);
    for (int i = 1; i <= REC_MEAS_NR; i++)
        printf(" x3m%d=%08x", i - 1, meas[i]);
    printf("\n");
}
