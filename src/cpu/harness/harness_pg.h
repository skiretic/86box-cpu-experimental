/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the paging state a case leaves (CR2, CR3,
 *          CR4, a result block and watched page-table dwords), the lookup
 *          tables as a run leaves them, and a lookup flush from another
 *          thread.
 */
#ifndef CPU_HARNESS_PG_H
#define CPU_HARNESS_PG_H

#include <stdint.h>

/* Record page: 16 dwords a case stores its own results in (a page-table
   entry, an error code its own handler saw). load_case() zeroes them with
   the rest of the page. */
#define REC_RES         0x100
#define HARNESS_RES_NR  16
#define HARNESS_WATCH_MAX 16

/* A device whose lookup flush comes from a thread other than the CPU's,
   as mem_mapping_recalc() does from a device thread: an IN from this port
   flushes the lookup tables from a second thread while the CPU thread waits
   for it, and reads as open bus. The CPU thread's own one-entry caches and
   the entries an instruction has already taken stay as they are. Always
   live, as the CPU-thread flush at 0xef is: the generator draws no IN, and
   no hand case reads port 0xf0 without meaning it. */
#define HARNESS_PORT_FLUSH_OTHER 0xf0

/* --dev: a device window of four pages on the external bus at this physical
   address. Every write to it is logged in order (its size, offset and
   value) into a count and a hash, which are compared between the runs; a
   read returns the count so far. */
#define HARNESS_DEV      0xfe000000
#define HARNESS_DEV_SIZE 0x4000
/* A write to this offset of the window also raises SMI#, as a device that
   traps an access does. */
#define HARNESS_DEV_SMI 0x3f00

typedef struct harness_pg_t {
    uint32_t cr0_val; /* printed with the rest; harness.c compares CR0 */
    uint32_t cr2_val;
    uint32_t cr3_val;
    uint32_t cr4_val;
    uint32_t res[HARNESS_RES_NR];
    uint32_t watch[HARNESS_WATCH_MAX];
    uint32_t dev_n;    /* --dev: the writes the device took */
    uint32_t dev_last; /* the offset and size of the last one, (offset << 4) | size */
    uint64_t dev_hash; /* over every write in order */
} harness_pg_t;

/* --pg: the fields above are captured, compared and printed. Without it
   they stay zero and nothing is printed. */
extern int harness_pg_on;
extern int harness_dev_on;
/* --dev-norun / --dev-run-max: whether the window takes runs of dword
   writes in one call, and at most how many per call. */
extern int      harness_dev_run;
extern uint32_t harness_dev_run_max;

extern void harness_dev_setup(void);
extern void harness_dev_reset(void);

extern int  harness_pg_option(int argc, char **argv, int *i);
extern void harness_pg_usage(void);
extern void harness_pg_capture(harness_pg_t *p);
extern int  harness_pg_differs(const harness_pg_t *a, const harness_pg_t *b);
extern void harness_pg_compare(const harness_pg_t *a, const harness_pg_t *b,
                               void (*diff)(const char *field, uint64_t a, uint64_t b));
extern void harness_pg_state_line(const char *run_name, const harness_pg_t *p);
extern void harness_flush_other(void);

#endif /* CPU_HARNESS_PG_H */
