/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the timing-model meter (--meter). It reads a
 *          book file of instruction forms with the clocks, uops or decoder
 *          class a CPU's own book gives them, measures what the selected
 *          CPU's recompiler timing model charges for each, and what the
 *          same instructions cost end to end compiled and interpreted, and
 *          prints one line per form with the verdict.
 */
#ifndef CPU_HARNESS_METER_H
#define CPU_HARNESS_METER_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

/* What a model's table holds for one instruction, as the opcode hook is
   handed it: last_prefix is 0, 0x0f or the x87 escape, nprefix the number
   of prefix() calls before the opcode (0F and escapes included), as the
   models count them for their decoders' length limits. Writes key=value
   pairs separated by ';' into out. */
typedef int (*meter_struct_fn)(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc,
                               int nprefix, char *out, size_t n);

#define METER_STRUCT_ARGS uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc, int nprefix, char *out, size_t n

extern int meter_struct_486(METER_STRUCT_ARGS);
extern int meter_struct_winchip(METER_STRUCT_ARGS);
extern int meter_struct_winchip2(METER_STRUCT_ARGS);
extern int meter_struct_pentium(METER_STRUCT_ARGS);
extern int meter_struct_686(METER_STRUCT_ARGS);
extern int meter_struct_p6(METER_STRUCT_ARGS);
extern int meter_struct_k6(METER_STRUCT_ARGS);

/* The x87_timings and x87_concurrency tables cpu_set() chose, and the
   timing_* variables, one line each; the name of the x87 table. */
extern void        meter_dump_x87(void);
extern void        meter_dump_vars(void);
extern const char *meter_x87_table_name(void);

/* An append helper for the readers. */
extern void meter_putf(char *out, size_t n, const char *fmt, ...);

/* The harness side (harness.c): one body run as a hand case, the
   interpreter's and the compiled run's TSC deltas. Returns 0 when it ran
   clean (halted at the epilogue's HLT, no fault, every run agreeing,
   converged), else a short reason in why. */
typedef struct meter_wire_t {
    uint64_t interp;
    uint64_t compiled;
    char     why[16];
} meter_wire_t;

/* An x87 row's start state for its runs: the control word, TOP, and the
   tags and register values by physical index. */
typedef struct meter_x87_t {
    uint16_t npxc;
    int      top;
    uint8_t  tag[8];
    double   st[8];
} meter_x87_t;

/* Memory a row's runs start with: len bytes at the linear address addr,
   the first plen bytes of pat repeated. The address is in the data page,
   the stack page, or a page from 0x9000 below 0x20000 (an extra page the
   case owns, zeroed); the bytes stop at the end of that page. */
#define METER_SEED_MAX  108
#define METER_SEEDS_MAX 4
typedef struct meter_seed_t {
    uint32_t addr;
    int      len;
    int      plen;
    uint8_t  pat[METER_SEED_MAX];
} meter_seed_t;

extern int harness_meter_wire(const uint8_t *body, int len, uint32_t ecx, uint32_t eflags, const meter_x87_t *x87,
                              const meter_seed_t *seeds, int nseeds, uint64_t mm0, meter_wire_t *w);

/* The prelude and epilogue the harness puts around a 32-bit body, as
   bytes. */
extern void harness_meter_frame(uint8_t *pre, int *npre, uint8_t *epi, int *nepi);

/* While set, a fatal() from the core longjmps back to the meter instead of
   ending the process (the timing models' own checks). */
extern void (*harness_fatal_hook)(const char *msg);

/* The meter itself (meter.c): a book file on the CPU set up. rider: this
   part has no book of its own and is measured on another part's rows,
   never scored; nobook: no book speaks for this part's model at all, every
   verdict reads NO-BOOK. Returns the process exit code. */
extern int meter_run(const char *book, const char *parts, int all_rows, int no_wire, int rider, int nobook,
                     const char *family, const char *cpu_name);

#endif /* CPU_HARNESS_METER_H */
