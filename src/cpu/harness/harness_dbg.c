/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the debug state of a run. With --dbg-state the
 *          runs are also compared on the debug registers, CR2, CR4, the CPU
 *          loop's pending trap, the frame the innermost handler left on the
 *          stack and the port devices' counts, and the dumps print them.
 *          --state-lines prints one line per run with them and the main
 *          registers, for an expectation table to be judged against.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"

#include "harness.h"
#include "harness_dbg.h"

static int dbg_state;
static int state_lines;

uint32_t harness_port_reads;
uint32_t harness_port_writes;
uint32_t harness_flush_reads;

int
harness_dbg_option(const char *arg)
{
    if (!strcmp(arg, "--dbg-state"))
        dbg_state = 1;
    else if (!strcmp(arg, "--state-lines"))
        state_lines = dbg_state = 1;
    else
        return 0;
    return 1;
}

void
harness_dbg_usage(void)
{
    fprintf(stderr,
            "  --dbg-state          also compare DR0-DR3, DR6, DR7, CR2, CR4, the pending trap, the\n"
            "                       four dwords at SS:ESP and the port device counts, and dump them\n"
            "  --state-lines        --dbg-state, and one RUNSTATE line per run (not with --quiet)\n");
}

/* A run starts with no trap pending and RF free to clear: what the run
   before left in either would otherwise act in this one. The port devices
   count from zero. */
void
harness_dbg_load(void)
{
    trap                = 0;
    rf_flag_no_clear    = 0;
    harness_port_reads  = 0;
    harness_port_writes = 0;
    harness_flush_reads = 0;
}

static uint32_t
ram32(uint32_t addr)
{
    if (addr > (mem_size * 1024) - 4)
        return 0xffffffff;
    return ram[addr] | (ram[addr + 1] << 8) | (ram[addr + 2] << 16) | ((uint32_t) ram[addr + 3] << 24);
}

/* The stack is read at its linear address as a physical one: the paging
   cases map the low pages one to one. */
void
harness_dbg_capture(harness_dbg_state_t *s, int status)
{
    const uint32_t sp = (stack32 ? ESP : (ESP & 0xffff)) + cpu_state.seg_ss.base;

    if (!dbg_state)
        return;
    s->captured = 1;
    memcpy(s->dr, dr, sizeof(s->dr));
    s->cr2  = cr2;
    s->cr4  = cr4;
    s->trap = trap;
    for (int i = 0; i < 4; i++)
        s->frame[i] = ram32(sp + i * 4);
    s->port_reads  = harness_port_reads;
    s->port_writes = harness_port_writes;
    s->flush_reads = harness_flush_reads;

    s->status = status;
    s->vector = ram32(HARNESS_RECORD + REC_VECTOR);
    s->tos    = ram32(HARNESS_RECORD + REC_TOS);
    s->eip    = cpu_state.pc;
    s->eflags = ((uint32_t) cpu_state.eflags << 16) | cpu_state.flags;
    for (int i = 0; i < 8; i++)
        s->regs[i] = cpu_state.regs[i].l;
    s->cr0_val = cr0;
    s->tsc = ram32(HARNESS_RECORD + REC_TSC_DELTA) | ((uint64_t) ram32(HARNESS_RECORD + REC_TSC_DELTA + 4) << 32);
    s->d100 = ram32(HARNESS_DATA + 0x100);
    for (int i = 0; i < HARNESS_LOG_WORDS; i++)
        s->log[i] = ram32(HARNESS_DATA + HARNESS_LOG_OFF + i * 4);
}

static const char *dr_names[8] = { "DR0", "DR1", "DR2", "DR3", "DR4", "DR5", "DR6", "DR7" };

int
harness_dbg_compare(const harness_dbg_state_t *a, const harness_dbg_state_t *b,
                    void (*diff)(const char *field, uint64_t a, uint64_t b))
{
    char name[16];
    int  n = 0;

    for (int i = 0; i < 8; i++) {
        if ((i == 4) || (i == 5) || (a->dr[i] == b->dr[i]))
            continue;
        diff(dr_names[i], a->dr[i], b->dr[i]);
        n++;
    }
    if (a->cr2 != b->cr2) {
        diff("CR2", a->cr2, b->cr2);
        n++;
    }
    if (a->cr4 != b->cr4) {
        diff("CR4", a->cr4, b->cr4);
        n++;
    }
    if (a->trap != b->trap) {
        diff("trap", a->trap, b->trap);
        n++;
    }
    for (int i = 0; i < 4; i++) {
        if (a->frame[i] == b->frame[i])
            continue;
        snprintf(name, sizeof(name), "frame%d", i);
        diff(name, a->frame[i], b->frame[i]);
        n++;
    }
    if (a->port_reads != b->port_reads) {
        diff("port.reads", a->port_reads, b->port_reads);
        n++;
    }
    if (a->port_writes != b->port_writes) {
        diff("port.writes", a->port_writes, b->port_writes);
        n++;
    }
    if (a->flush_reads != b->flush_reads) {
        diff("flush.reads", a->flush_reads, b->flush_reads);
        n++;
    }
    return n;
}

static void
diff_none(const char *field, uint64_t a, uint64_t b)
{
    (void) field;
    (void) a;
    (void) b;
}

int
harness_dbg_differs(const harness_dbg_state_t *a, const harness_dbg_state_t *b)
{
    return !!harness_dbg_compare(a, b, diff_none);
}

void
harness_dbg_dump(const harness_dbg_state_t *s)
{
    if (!s->captured)
        return;
    printf("    DR0=%08x DR1=%08x DR2=%08x DR3=%08x DR6=%08x DR7=%08x CR2=%08x CR4=%08x\n",
           s->dr[0], s->dr[1], s->dr[2], s->dr[3], s->dr[6], s->dr[7], s->cr2, s->cr4);
    printf("    EFLAGS=%08x trap=%x frame=%08x %08x %08x %08x port.reads=%u port.writes=%u flush.reads=%u\n",
           s->eflags, s->trap, s->frame[0], s->frame[1], s->frame[2], s->frame[3], s->port_reads, s->port_writes, s->flush_reads);
}

/* The run's name as one token: "interpreter" interp, "mark pass" mark,
   "compile pass" compile, "compile pass N" compileN, "compiled" compiled,
   "compiled 2" compiled2. */
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

void
harness_dbg_state_line(const char *run_name, uint32_t body, const harness_dbg_state_t *s)
{
    static const char *status_names[3] = { "halted", "timeout", "reset" };
    static const char *reg_names[8]    = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    char               tok[24];

    if (!state_lines || !s->captured)
        return;
    run_token(run_name, tok, sizeof(tok));
    printf("RUNSTATE %s body=%08x status=%s vector=%08x tos=%08x eip=%08x eflags=%08x", tok, body,
           (s->status < 3) ? status_names[s->status] : "?", s->vector, s->tos, s->eip, s->eflags);
    for (int i = 0; i < 8; i++)
        printf(" %s=%08x", reg_names[i], s->regs[i]);
    printf(" cr0=%08x cr2=%08x cr4=%08x", s->cr0_val, s->cr2, s->cr4);
    for (int i = 0; i < 4; i++)
        printf(" dr%d=%08x", i, s->dr[i]);
    printf(" dr6=%08x dr7=%08x trap=%08x", s->dr[6], s->dr[7], s->trap);
    for (int i = 0; i < 4; i++)
        printf(" frame%d=%08x", i, s->frame[i]);
    printf(" preads=%08x pwrites=%08x freads=%08x tsc=%" PRIu64 " d100=%08x", s->port_reads, s->port_writes, s->flush_reads, s->tsc, s->d100);
    for (int i = 0; i < HARNESS_LOG_WORDS; i++)
        printf(" d%03x=%08x", HARNESS_LOG_OFF + i * 4, s->log[i]);
    printf("\n");
}
