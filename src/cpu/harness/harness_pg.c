/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the paging state a case leaves, the lookup
 *          tables as a run leaves them, and a lookup flush from another
 *          thread.
 *
 *          --pg captures CR2, CR3 and CR4 at the halt and the record
 *          page's result block, compares them between the runs and
 *          prints them, with CR0, as one RUNSTATE line per run; --watch
 *          adds physical dwords (page-table entries) read before the
 *          stray-page check puts their pages back.
 *          --lookups prints, to stderr after every run, the read and
 *          write lookup entries of the linear pages given: which physical
 *          page each one maps, or none. That is a path check only, never
 *          compared: the recompiler's dispatch installs entries the
 *          interpreter does not.
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"

#include "harness.h"
#include "harness_pg.h"

int harness_pg_on = 0;

static uint32_t watch_addr[HARNESS_WATCH_MAX];
static int      watch_nr;
static uint32_t lookup_lin[HARNESS_WATCH_MAX];
static int      lookup_nr;
/* --flush-at: the site and the call of it, from 1, that flushes; the calls
   of that site counted in the run so far. */
static int      flush_site;
static uint32_t flush_call;
static uint32_t flush_count;

#ifdef CPU_HAS_MEM_TEST_HOOK
static void
flush_at_hook(int site)
{
    if ((site == flush_site) && (++flush_count == flush_call)) {
        harness_flush_other();
        fprintf(stderr, "flush-at: site %d call %u flushed\n", site, flush_call);
    }
}
#endif

/* A comma separated list of hex addresses into out[], at most max. Returns
   the count, or -1. */
static int
parse_addr_list(const char *s, uint32_t *out, int max)
{
    int n = 0;

    while (*s) {
        char         *end;
        unsigned long v = strtoul(s, &end, 16);

        if ((end == s) || (n >= max) || (v > 0xffffffffUL))
            return -1;
        out[n++] = (uint32_t) v;
        s        = end;
        if (*s == ',')
            s++;
        else if (*s)
            return -1;
    }
    return n;
}

/* Returns 1 when argv[*i] is one of this file's options (taken, with its
   argument), 0 when it is not, -1 when it is but its argument is bad. */
int
harness_pg_option(int argc, char **argv, int *i)
{
    if (!strcmp(argv[*i], "--pg")) {
        harness_pg_on = 1;
        return 1;
    }
    if (!strcmp(argv[*i], "--watch") && (*i + 1 < argc)) {
        watch_nr = parse_addr_list(argv[++*i], watch_addr, HARNESS_WATCH_MAX);
        if (watch_nr <= 0) {
            fprintf(stderr, "--watch takes 1 to %d physical addresses, hex, comma separated\n", HARNESS_WATCH_MAX);
            return -1;
        }
        harness_pg_on = 1;
        return 1;
    }
    if (!strcmp(argv[*i], "--flush-at") && (*i + 1 < argc)) {
        char *end;

        flush_site = (int) strtol(argv[++*i], &end, 10);
        if ((flush_site < 1) || (flush_site > 4) || (*end != ':')
            || !(flush_call = (uint32_t) strtoul(end + 1, &end, 10)) || *end) {
            fprintf(stderr, "--flush-at takes <site 1-4>:<call from 1>\n");
            return -1;
        }
#ifdef CPU_HAS_MEM_TEST_HOOK
        mem_test_hook = flush_at_hook;
        return 1;
#else
        fprintf(stderr, "--flush-at needs a harness built with scripts/harness-flush-hook.diff\n");
        return -1;
#endif
    }
    if (!strcmp(argv[*i], "--lookups") && (*i + 1 < argc)) {
        lookup_nr = parse_addr_list(argv[++*i], lookup_lin, HARNESS_WATCH_MAX);
        if (lookup_nr <= 0) {
            fprintf(stderr, "--lookups takes 1 to %d linear addresses, hex, comma separated\n", HARNESS_WATCH_MAX);
            return -1;
        }
        return 1;
    }
    return 0;
}

void
harness_pg_usage(void)
{
    fprintf(stderr,
            "  --pg                 also capture, compare and print CR2, CR3, CR4 at the halt and the\n"
            "                       record page's result block (16 dwords at %05x); printed as RUNSTATE\n"
            "                       lines, which --quiet leaves out for a case whose runs all agree\n"
            "  --watch <a>[,<b>...] (implies --pg) also the dwords at these physical addresses (hex,\n"
            "                       up to %d): page-table entries as the run left them\n"
            "  --lookups <a>[,...]  after every run, to stderr: the read and write lookup entries of\n"
            "                       these linear pages (hex, up to %d); never compared\n"
            "  --flush-at <s>:<n>   the n-th call in a run of the flush hook at site s (1 checkio() word\n"
            "                       read, 2 its byte read, 3 a string op's source, 4 its destination)\n"
            "                       flushes the lookup tables from another thread; needs a build with\n"
            "                       scripts/harness-flush-hook.diff\n",
            HARNESS_RECORD + REC_RES, HARNESS_WATCH_MAX, HARNESS_WATCH_MAX);
}

static uint32_t
phys_dword(uint32_t addr)
{
    if ((uint64_t) addr + 4 > (uint64_t) mem_size * 1024)
        return 0xffffffff;
    return ram[addr] | (ram[addr + 1] << 8) | (ram[addr + 2] << 16) | ((uint32_t) ram[addr + 3] << 24);
}

/* The physical page a lookup entry maps (the entry is the host address of
   the page's RAM less the linear page address), or none. */
static void
print_lookup(const char *name, uintptr_t e, uint32_t lin)
{
    if (e == (uintptr_t) LOOKUP_INV)
        fprintf(stderr, " %s=-", name);
    else
        fprintf(stderr, " %s=%08" PRIx64, name, (uint64_t) (e + (lin & ~0xfff) - (uintptr_t) ram));
}

/* Before the stray-page check puts the page tables back. */
void
harness_pg_capture(harness_pg_t *p)
{
    /* The next run counts its hook calls from its start. */
    flush_count = 0;
    memset(p, 0, sizeof(*p));
    if (harness_pg_on) {
        p->cr0_val = cr0;
        p->cr2_val = cr2;
        p->cr3_val = cr3;
        p->cr4_val = cr4;
        for (int i = 0; i < HARNESS_RES_NR; i++)
            p->res[i] = phys_dword(HARNESS_RECORD + REC_RES + i * 4);
        for (int i = 0; i < watch_nr; i++)
            p->watch[i] = phys_dword(watch_addr[i]);
    }
    if (lookup_nr) {
        fprintf(stderr, "lookups:");
        for (int i = 0; i < lookup_nr; i++) {
            const uint32_t l = lookup_lin[i];

            fprintf(stderr, " [%08x", l & ~0xfff);
            print_lookup("r", readlookup2[l >> 12], l);
            print_lookup("r2", readlookup2[(l >> 12) | 1048576], l);
            print_lookup("w", writelookup2[l >> 12], l);
            fprintf(stderr, "]");
        }
        fprintf(stderr, "\n");
    }
}

int
harness_pg_differs(const harness_pg_t *a, const harness_pg_t *b)
{
    return memcmp(a, b, sizeof(*a)) != 0;
}

void
harness_pg_compare(const harness_pg_t *a, const harness_pg_t *b, void (*diff)(const char *field, uint64_t a, uint64_t b))
{
    char name[32];

    if (a->cr2_val != b->cr2_val)
        diff("CR2", a->cr2_val, b->cr2_val);
    if (a->cr3_val != b->cr3_val)
        diff("CR3", a->cr3_val, b->cr3_val);
    if (a->cr4_val != b->cr4_val)
        diff("CR4", a->cr4_val, b->cr4_val);
    for (int i = 0; i < HARNESS_RES_NR; i++) {
        if (a->res[i] != b->res[i]) {
            snprintf(name, sizeof(name), "RES%d", i);
            diff(name, a->res[i], b->res[i]);
        }
    }
    for (int i = 0; i < watch_nr; i++) {
        if (a->watch[i] != b->watch[i]) {
            snprintf(name, sizeof(name), "W%08x", watch_addr[i]);
            diff(name, a->watch[i], b->watch[i]);
        }
    }
}

/* The run's name as one token, as the kit's expectations name runs:
   "interpreter" interp, "mark pass" mark, "compile pass" compile, "compile
   pass N" compileN, "compiled" compiled, "compiled 2" compiled2. */
static void
run_token(const char *name, char *tok, size_t size)
{
    if (!strcmp(name, "interpreter"))
        snprintf(tok, size, "interp");
    else if (!strcmp(name, "mark pass"))
        snprintf(tok, size, "mark");
    else if (!strncmp(name, "compile pass", 12))
        snprintf(tok, size, "compile%s", name[12] ? name + 13 : "");
    else if (!strcmp(name, "compiled 2"))
        snprintf(tok, size, "compiled2");
    else
        snprintf(tok, size, "%s", name);
}

/* --pg: one line per run, "RUNSTATE <run> <field>=<hex> ...". */
void
harness_pg_state_line(const char *run_name, const harness_pg_t *p)
{
    char tok[24];

    if (!harness_pg_on)
        return;
    run_token(run_name, tok, sizeof(tok));
    printf("RUNSTATE %s cr0=%08x cr2=%08x cr3=%08x cr4=%08x", tok, p->cr0_val, p->cr2_val, p->cr3_val, p->cr4_val);
    for (int i = 0; i < HARNESS_RES_NR; i++)
        printf(" res%d=%08x", i, p->res[i]);
    for (int i = 0; i < watch_nr; i++)
        printf(" w%08x=%08x", watch_addr[i], p->watch[i]);
    printf("\n");
}

/* The flush a device thread makes: is_cpu_thread is clear on that thread,
   so mem.c leaves the CPU thread's one-entry caches alone. The CPU thread
   waits for it, so the flush lands at one point of the instruction, the
   port read, every run. */
static void *
flush_other_thread(void *arg)
{
    (void) arg;
    is_cpu_thread = 0;
    flushmmucache_nopc();
    return NULL;
}

void
harness_flush_other(void)
{
    pthread_t t;

    if (pthread_create(&t, NULL, flush_other_thread, NULL) || pthread_join(t, NULL))
        harness_fatal("the flush thread could not be run");
}
