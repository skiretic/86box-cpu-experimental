/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the debug state a run is compared on with
 *          --dbg-state, the per-run state lines of --state-lines, and the
 *          harness's port devices on io.c.
 */
#ifndef CPU_HARNESS_DBG_H
#define CPU_HARNESS_DBG_H

#include <stdint.h>

/* The counting device: every IN of any size from these ports, and every
   OUT to them, is counted. Reads give open bus. Away from E0h-EFh, where a
   word or dword access to one device port would also cover the next. */
#define HARNESS_PORT_COUNT      0x0500
#define HARNESS_PORT_COUNT_SIZE 8

/* Custom handlers log here: the data page from 400h, 64 dwords. */
#define HARNESS_LOG_OFF   0x400
#define HARNESS_LOG_WORDS 64

typedef struct harness_dbg_state_t {
    uint32_t captured;
    uint32_t dr[8];
    uint32_t cr2;
    uint32_t cr4;
    uint32_t trap;
    uint32_t frame[4];   /* the dwords at SS:ESP at the halt */
    uint32_t port_reads;
    uint32_t port_writes;
    uint32_t flush_reads;
    /* Printed, not compared here (the main state holds them, or they are
       per instruction). */
    uint32_t status;
    uint32_t vector;
    uint32_t tos;
    uint32_t eip;
    uint32_t eflags; /* the whole register, RF included */
    uint32_t regs[8];
    uint32_t cr0_val;
    uint64_t tsc;
    uint32_t d100;
    uint32_t log[HARNESS_LOG_WORDS];
} harness_dbg_state_t;

/* Options: returns 1 for one of this file's options. */
extern int  harness_dbg_option(const char *arg);
extern void harness_dbg_usage(void);

/* load_case(): what a run starts from, beside the debug registers. */
extern void harness_dbg_load(void);
/* capture(): the debug state, with --dbg-state or --state-lines. */
extern void harness_dbg_capture(harness_dbg_state_t *s, int status);
/* compare(): the differing fields through diff(); returns their number. */
extern int  harness_dbg_compare(const harness_dbg_state_t *a, const harness_dbg_state_t *b,
                                void (*diff)(const char *field, uint64_t a, uint64_t b));
extern int  harness_dbg_differs(const harness_dbg_state_t *a, const harness_dbg_state_t *b);
extern void harness_dbg_dump(const harness_dbg_state_t *s);
/* --state-lines: one RUNSTATE line for a run. */
extern void harness_dbg_state_line(const char *run_name, uint32_t body, const harness_dbg_state_t *s);

/* The port devices' counts since the run began. */
extern uint32_t harness_port_reads;
extern uint32_t harness_port_writes;
extern uint32_t harness_flush_reads;

#endif /* CPU_HARNESS_DBG_H */
