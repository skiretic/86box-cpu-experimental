/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: runs a code blob from one snapshot of CPU
 *          and memory state under the interpreter, then under the
 *          recompiler's compile pass and its compiled code, and diffs
 *          the resulting state.
 *
 *          The guest is a flat 32-bit (or 16-bit) protected-mode
 *          environment built straight into RAM: a GDT, an IDT whose
 *          every gate lands on a handler that records the vector and
 *          halts, a stack page, a data page and a code page. The blob
 *          sits between a prelude and an epilogue that bracket it with
 *          RDTSC and end in HLT; the driver runs cycle slices until the
 *          CPU halts.
 *
 *          Architectural state (registers, flags, segments, x87, MMX, the
 *          data page, the fault record) must agree between every run.
 *          The RDTSC delta must agree between the compile pass and the
 *          compiled code, which charge the same timing model; the
 *          interpreter charges per instruction and its delta is only
 *          reported.
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include "386_common.h"
#include "x86_flags.h"
#include "codegen.h"

#include "harness.h"

int harness_verbose = 0;

static const harness_case_t *cur_case  = NULL;
static const char           *cur_phase = "setup";

enum {
    RUN_HALTED = 0,
    RUN_TIMEOUT,
    RUN_RESET
};

typedef struct harness_state_t {
    int      status;
    uint32_t regs[8];
    uint16_t flags;
    uint16_t eflags;
    uint16_t seg[6];
    uint32_t base[6];
    uint32_t pc;
    uint32_t cr0_val;
    uint8_t  abrt;
    uint16_t npxc;
    uint16_t npxs;
    int      top;
    uint8_t  tag[8];
    uint64_t st[8];
    uint64_t mm[8];
    uint8_t  ismmx;
    uint32_t vector;
    uint32_t tos;
    uint64_t tsc_delta;
    uint32_t stray_pages; /* pages written outside the ones the case owns */
    uint32_t stray_first; /* the lowest of them */
    uint64_t stray_hash;  /* over their addresses and contents */
    uint8_t  data[HARNESS_PAGE];
} harness_state_t;

static const char *run_names[] = { "interpreter", "compile pass", "compiled", "compiled 2" };

/* Slice sizes: the recompiler loop splits its argument into 200 periods
   and needs each to be at least a cycle. */
#define SLICE_DYN 20000
#define SLICE_INT 2000
#define MAX_SLICES 4000

static void
dump_case_hex(const harness_case_t *c)
{
    fprintf(stderr, "  body:");
    for (int i = 0; i < c->body_len; i++)
        fprintf(stderr, "%02x", c->body[i]);
    fprintf(stderr, "\n");
}

void
harness_fatal(const char *msg)
{
    fprintf(stderr, "FATAL from the core during %s: %s\n", cur_phase, msg);
    if (cur_case) {
        fprintf(stderr, "  case: %s\n", cur_case->label);
        dump_case_hex(cur_case);
    }
    exit(3);
}

/* ---- guest tables ---------------------------------------------------- */

static void
put16(uint32_t addr, uint16_t v)
{
    ram[addr]     = v & 0xff;
    ram[addr + 1] = v >> 8;
}

static void
put32(uint32_t addr, uint32_t v)
{
    put16(addr, v & 0xffff);
    put16(addr + 2, v >> 16);
}

static uint32_t
get32(uint32_t addr)
{
    return ram[addr] | (ram[addr + 1] << 8) | (ram[addr + 2] << 16) | ((uint32_t) ram[addr + 3] << 24);
}

static void
gdt_entry(int sel, uint8_t access, uint8_t flags)
{
    uint32_t a = HARNESS_GDT + sel;

    put16(a, 0xffff);     /* limit 15:0 */
    put16(a + 2, 0);      /* base 15:0 */
    ram[a + 4] = 0;       /* base 23:16 */
    ram[a + 5] = access;
    ram[a + 6] = flags | 0x0f; /* G, D/B; limit 19:16 */
    ram[a + 7] = 0;       /* base 31:24 */
}

static void
build_tables(void)
{
    /* GDT: null, flat 32-bit code and data, flat 16-bit code and data
       (same 4 GB limit, D/B clear). */
    memset(ram + HARNESS_GDT, 0, 8);
    /* Accessed bits set: a load of a descriptor without it writes the
       table, and the GDT page would count as a store the case made. */
    gdt_entry(SEL_CODE32, 0x9b, 0xc0);
    gdt_entry(SEL_DATA32, 0x93, 0xc0);
    gdt_entry(SEL_CODE16, 0x9b, 0x80);
    gdt_entry(SEL_DATA16, 0x93, 0x80);

    /* IDT: every vector a 32-bit interrupt gate into the flat code segment,
       each to its own handler. */
    for (int v = 0; v < 256; v++) {
        uint32_t a = HARNESS_IDT + v * 8;
        uint32_t h = HARNESS_HANDLERS + v * 32;

        put16(a, h & 0xffff);
        put16(a + 2, SEL_CODE32);
        ram[a + 4] = 0;
        ram[a + 5] = 0x8e;
        put16(a + 6, h >> 16);
    }

    /* Handlers: record the vector and the dword on top of the stack (an
       error code, or the return EIP when there is none), then halt. */
    for (int v = 0; v < 256; v++) {
        uint8_t *h = ram + HARNESS_HANDLERS + v * 32;
        int      n = 0;

        memset(h, 0x90, 32);
        h[n++] = 0xc7; h[n++] = 0x05; /* mov dword [REC_VECTOR], v */
        put32(HARNESS_HANDLERS + v * 32 + n, HARNESS_RECORD + REC_VECTOR); n += 4;
        put32(HARNESS_HANDLERS + v * 32 + n, v); n += 4;
        h[n++] = 0x8b; h[n++] = 0x04; h[n++] = 0x24; /* mov eax, [esp] */
        h[n++] = 0xa3;                               /* mov [REC_TOS], eax */
        put32(HARNESS_HANDLERS + v * 32 + n, HARNESS_RECORD + REC_TOS); n += 4;
        h[n++] = 0xf4;                               /* hlt */
    }
}

/* Prelude and epilogue around the body. In a 16-bit code segment every
   memory form gets 67 66 (32-bit address and operand) so the same bytes
   mean the same thing. */
static int
emit_mem_op(uint8_t *p, int mode16, const uint8_t *op, int oplen, uint32_t disp)
{
    int n = 0;

    if (mode16) {
        p[n++] = 0x67;
        p[n++] = 0x66;
    }
    memcpy(p + n, op, oplen);
    n += oplen;
    p[n++] = disp & 0xff;
    p[n++] = (disp >> 8) & 0xff;
    p[n++] = (disp >> 16) & 0xff;
    p[n++] = disp >> 24;
    return n;
}

static const uint8_t op_mov_m_eax[] = { 0xa3 };       /* mov [disp32], eax */
static const uint8_t op_mov_m_edx[] = { 0x89, 0x15 }; /* mov [disp32], edx */
static const uint8_t op_mov_eax_m[] = { 0xa1 };       /* mov eax, [disp32] */
static const uint8_t op_mov_edx_m[] = { 0x8b, 0x15 }; /* mov edx, [disp32] */
static const uint8_t op_sub_eax_m[] = { 0x2b, 0x05 }; /* sub eax, [disp32] */
static const uint8_t op_sbb_edx_m[] = { 0x1b, 0x15 }; /* sbb edx, [disp32] */

static int
emit_prelude(uint8_t *p, int mode16)
{
    int n = 0;

    n += emit_mem_op(p + n, mode16, op_mov_m_eax, 1, HARNESS_RECORD + REC_SAVE_EAX);
    n += emit_mem_op(p + n, mode16, op_mov_m_edx, 2, HARNESS_RECORD + REC_SAVE_EDX);
    p[n++] = 0x0f; p[n++] = 0x31; /* rdtsc */
    n += emit_mem_op(p + n, mode16, op_mov_m_eax, 1, HARNESS_RECORD + REC_TSC_START);
    n += emit_mem_op(p + n, mode16, op_mov_m_edx, 2, HARNESS_RECORD + REC_TSC_START + 4);
    n += emit_mem_op(p + n, mode16, op_mov_eax_m, 1, HARNESS_RECORD + REC_SAVE_EAX);
    n += emit_mem_op(p + n, mode16, op_mov_edx_m, 2, HARNESS_RECORD + REC_SAVE_EDX);
    return n;
}

static int
emit_epilogue(uint8_t *p, int mode16)
{
    int n = 0;

    if (mode16)
        p[n++] = 0x66;
    p[n++] = 0x9c; /* pushfd */
    n += emit_mem_op(p + n, mode16, op_mov_m_eax, 1, HARNESS_RECORD + REC_SAVE_EAX2);
    n += emit_mem_op(p + n, mode16, op_mov_m_edx, 2, HARNESS_RECORD + REC_SAVE_EDX2);
    p[n++] = 0x0f; p[n++] = 0x31; /* rdtsc */
    n += emit_mem_op(p + n, mode16, op_sub_eax_m, 2, HARNESS_RECORD + REC_TSC_START);
    n += emit_mem_op(p + n, mode16, op_sbb_edx_m, 2, HARNESS_RECORD + REC_TSC_START + 4);
    n += emit_mem_op(p + n, mode16, op_mov_m_eax, 1, HARNESS_RECORD + REC_TSC_DELTA);
    n += emit_mem_op(p + n, mode16, op_mov_m_edx, 2, HARNESS_RECORD + REC_TSC_DELTA + 4);
    n += emit_mem_op(p + n, mode16, op_mov_eax_m, 1, HARNESS_RECORD + REC_SAVE_EAX2);
    n += emit_mem_op(p + n, mode16, op_mov_edx_m, 2, HARNESS_RECORD + REC_SAVE_EDX2);
    if (mode16)
        p[n++] = 0x66;
    p[n++] = 0x9d; /* popfd */
    p[n++] = 0xf4; /* hlt */
    return n;
}

/* ---- machine --------------------------------------------------------- */

static uint8_t *ram_ref; /* RAM as set up, before any case ran */

static void
step(const char *what)
{
    cur_phase = what;
    if (harness_verbose)
        fprintf(stderr, "harness: %s\n", what);
}

static void
setup_machine(int softfloat)
{
    const CPU *cpus;
    int        n;

    mem_size = HARNESS_RAM_KB;
    cpu_f    = cpu_get_family("pentium2_deschutes");
    if (cpu_f == NULL) {
        fprintf(stderr, "no pentium2_deschutes family in the CPU table\n");
        exit(2);
    }
    cpus = cpu_f->cpus;
    for (n = 0; cpus[n].name[0]; n++) {
        if (!strcmp(cpus[n].name, "600"))
            break;
    }
    if (!cpus[n].name[0]) {
        fprintf(stderr, "no 600 MHz Deschutes in the CPU table\n");
        exit(2);
    }
    cpu             = n;
    fpu_type        = FPU_INTERNAL;
    fpu_softfloat   = softfloat;
    cpu_use_dynarec = 1;

    step("mem_init");
    mem_init();
    step("timer_init");
    timer_init();
    step("cpu_set");
    cpu_set();
    step("mem_reset");
    mem_reset();
    /* The code memory is MAP_JIT: as 86box.c does, write it unprotected
       while the backend lays its routines down. The dynarec loop toggles
       it around every compile from here on. */
    step("codegen_init");
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(0);
#endif
    codegen_init();
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(1);
#endif
    step("resetx86");
    resetx86();

    step("tables");
    build_tables();

    /* The reference every page the cases do not own is held to. */
    ram_ref = malloc(mem_size * 1024);
    if (ram_ref == NULL)
        harness_fatal("no memory for the RAM reference");
    memcpy(ram_ref, ram, mem_size * 1024);
}

/* ---- case state ------------------------------------------------------ */

static uint8_t code_image[HARNESS_PAGE];
static int     code_len;

static void
build_code(const harness_case_t *c)
{
    int n = 0;

    memset(code_image, 0xf4, sizeof(code_image)); /* HLT past the end */
    n += emit_prelude(code_image + n, c->mode16);
    memcpy(code_image + n, c->body, c->body_len);
    n += c->body_len;
    n += emit_epilogue(code_image + n, c->mode16);
    code_len = n;
}

static void
load_case(const harness_case_t *c, int with_code)
{
    cur_phase = "state load";

    if (with_code)
        memcpy(ram + HARNESS_CODE, code_image, HARNESS_PAGE);
    memcpy(ram + HARNESS_DATA, c->data, HARNESS_PAGE);
    memset(ram + HARNESS_STACK, 0, HARNESS_PAGE);
    memset(ram + HARNESS_RECORD, 0, HARNESS_PAGE);
    put32(HARNESS_RECORD + REC_VECTOR, 0xffffffff);

    cpu_state.abrt = 0;
    cpu_halted     = 0;
    x86_was_reset  = 0;
    cpu_init       = 0;
    cpu_flush_pending = 0;
    cycles         = 0;
    cycles_main    = 0;

    cpu_state.flags_op = FLAGS_UNKNOWN;
    cpu_state.flags    = (c->eflags & 0x0cd5) | 0x0002;
    cpu_state.eflags   = 0;

    /* PE, and NE so an unmasked x87 exception is #MF through the IDT.
       With NE clear it is FERR# and IRQ13, and with no PIC here the
       waiting instruction would stall forever. */
    cr0 |= 0x21;
    cr4 = 0;
    cr3 = 0;

    gdt.base  = HARNESS_GDT;
    gdt.limit = 0x27;
    idt.base  = HARNESS_IDT;
    idt.limit = 0x7ff;
    ldt.seg   = 0;
    ldt.base  = 0;
    ldt.limit = 0;
    tr.seg    = 0;
    tr.base   = 0;
    tr.limit  = 0;

    cpu_cur_status &= ~(CPU_STATUS_V86 | CPU_STATUS_SMM);
    cpu_cur_status |= CPU_STATUS_PMODE;

    loadcs(c->mode16 ? SEL_CODE16 : SEL_CODE32);
    loadseg(SEL_DATA32, &cpu_state.seg_ds);
    loadseg(SEL_DATA32, &cpu_state.seg_es);
    loadseg(SEL_DATA32, &cpu_state.seg_fs);
    loadseg(SEL_DATA32, &cpu_state.seg_gs);
    loadseg(c->mode16 ? SEL_DATA16 : SEL_DATA32, &cpu_state.seg_ss);
    if (cpu_state.abrt) {
        fprintf(stderr, "segment load faulted (abrt %02x) while loading the case\n", cpu_state.abrt);
        exit(2);
    }

    for (int i = 0; i < 8; i++)
        cpu_state.regs[i].l = c->regs[i];
    cpu_state.pc    = HARNESS_CODE;
    cpu_state.oldpc = HARNESS_CODE;
    cpu_state.op32  = use32;
    cpu_state.ea_seg = &cpu_state.seg_ds;
    cpu_state.ssegs  = 0;

    cpu_state.npxc = c->npxc;
    cpu_state.npxs = (c->top & 7) << 11;
    cpu_state.TOP  = c->top & 7;
    for (int i = 0; i < 8; i++) {
        cpu_state.ST[i]  = c->st[i];
        cpu_state.tag[i] = c->tag[i];
        cpu_state.MM[i].q = c->mm[i];
    }
    cpu_state.ismmx = 0;
    codegen_set_rounding_mode((c->npxc >> 10) & 3);
    fpu_ferr  = 0;
    fpu_ignne = 0;
    new_ne    = 0;
    x87_op    = 0;
    /* The last x87 instruction's pointers, what FNSTENV and FNSAVE store:
       a fresh FPU has none. */
    cpu_state.fpu_cs = 0;
    cpu_state.fpu_CS = 0;
    cpu_state.fpu_pc = 0;
    cpu_state.fpu_op = 0;
    cpu_state.fpu_DS = 0;
    cpu_state.fpu_ds = 0;
    cpu_state.fpu_ea = 0;
    cpu_state.temp_cs = 0;
    cpu_state.temp_CS = 0;
    cpu_state.temp_pc = 0;
}

/* The pages a case owns: load_case() reloads them, and they are compared
   through the state (data, record) or read by the CPU itself (code). */
static int
case_owns_page(uint32_t addr)
{
    return addr == HARNESS_RECORD || addr == HARNESS_STACK || addr == HARNESS_DATA || addr == HARNESS_CODE;
}

/* Every other page must still match the reference taken after setup. One
   that does not holds a store the case made outside its data page: it is
   counted and hashed into the state, so the runs are compared on it too,
   and put back, so the store cannot reach the next run. The page is marked
   dirty as a CPU write would: the case may have run code from it, and a
   block compiled from the bytes the memcpy replaces has to go. */
static void
capture_stray_pages(harness_state_t *s)
{
    uint32_t end = mem_size * 1024;

    s->stray_pages = 0;
    s->stray_first = 0;
    s->stray_hash  = 0xcbf29ce484222325ull;
    for (uint32_t a = 0; a < end; a += HARNESS_PAGE) {
        if (case_owns_page(a) || !memcmp(ram + a, ram_ref + a, HARNESS_PAGE))
            continue;
        if (!s->stray_pages)
            s->stray_first = a;
        s->stray_pages++;
        s->stray_hash = (s->stray_hash ^ a) * 0x100000001b3ull;
        for (int i = 0; i < HARNESS_PAGE; i++)
            s->stray_hash = (s->stray_hash ^ ram[a + i]) * 0x100000001b3ull;
        memcpy(ram + a, ram_ref + a, HARNESS_PAGE);
        mem_invalidate_range(a, a);
    }
}

static void
capture(harness_state_t *s, int status)
{
    x86seg *segs[6] = { &cpu_state.seg_cs, &cpu_state.seg_ds, &cpu_state.seg_es,
                        &cpu_state.seg_ss, &cpu_state.seg_fs, &cpu_state.seg_gs };

    flags_rebuild();
    memset(s, 0, sizeof(*s));
    s->status = status;
    for (int i = 0; i < 8; i++)
        s->regs[i] = cpu_state.regs[i].l;
    s->flags  = cpu_state.flags & 0x0fd5;
    s->eflags = cpu_state.eflags & ~1; /* RF is per instruction */
    for (int i = 0; i < 6; i++) {
        s->seg[i]  = segs[i]->seg;
        s->base[i] = segs[i]->base;
    }
    s->pc   = cpu_state.pc;
    s->cr0_val = cr0;
    s->abrt = cpu_state.abrt;
    s->npxc = cpu_state.npxc;
    s->npxs = cpu_state.npxs;
    s->top  = cpu_state.TOP;
    for (int i = 0; i < 8; i++) {
        s->tag[i] = cpu_state.tag[i];
        memcpy(&s->st[i], &cpu_state.ST[i], 8);
        s->mm[i] = cpu_state.MM[i].q;
    }
    s->ismmx     = cpu_state.ismmx;
    s->vector    = get32(HARNESS_RECORD + REC_VECTOR);
    s->tos       = get32(HARNESS_RECORD + REC_TOS);
    s->tsc_delta = get32(HARNESS_RECORD + REC_TSC_DELTA) | ((uint64_t) get32(HARNESS_RECORD + REC_TSC_DELTA + 4) << 32);
    memcpy(s->data, ram + HARNESS_DATA, HARNESS_PAGE);
    capture_stray_pages(s);
}

static int
run_until_halt(int dyn)
{
    int slices = 0;

    cpu_use_dynarec = dyn;
    for (;;) {
        cpu_halted   = 0;
        timer_target = tsc + 1000000; /* no timer is ever due */
        cycles       = 0;
        cycles_main  = 0;
        if (dyn)
            exec386_dynarec(SLICE_DYN);
        else
            exec386(SLICE_INT);
        if (x86_was_reset)
            return RUN_RESET;
        if (cpu_halted)
            return RUN_HALTED;
        if (++slices > MAX_SLICES)
            return RUN_TIMEOUT;
    }
}

/* ---- comparison ------------------------------------------------------ */

static const char *reg_names[8] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
static const char *seg_names[6] = { "CS", "DS", "ES", "SS", "FS", "GS" };
static const char *status_names[3] = { "halted", "timeout", "reset" };

static int diff_lines;

static void
diff(const char *field, uint64_t a, uint64_t b)
{
    if (diff_lines < 12)
        printf("    %-10s %016" PRIx64 " != %016" PRIx64 "\n", field, a, b);
    diff_lines++;
}

/* Returns the number of differing fields. */
static int
compare(const harness_state_t *a, const harness_state_t *b)
{
    char name[32];

    diff_lines = 0;
    if (a->status != b->status) {
        if (diff_lines < 12)
            printf("    status     %s != %s\n", status_names[a->status], status_names[b->status]);
        diff_lines++;
    }
    for (int i = 0; i < 8; i++)
        if (a->regs[i] != b->regs[i])
            diff(reg_names[i], a->regs[i], b->regs[i]);
    if (a->flags != b->flags)
        diff("FLAGS", a->flags, b->flags);
    if (a->eflags != b->eflags)
        diff("EFLAGS.hi", a->eflags, b->eflags);
    for (int i = 0; i < 6; i++) {
        if (a->seg[i] != b->seg[i])
            diff(seg_names[i], a->seg[i], b->seg[i]);
        if (a->base[i] != b->base[i]) {
            snprintf(name, sizeof(name), "%s.base", seg_names[i]);
            diff(name, a->base[i], b->base[i]);
        }
    }
    if (a->pc != b->pc)
        diff("EIP", a->pc, b->pc);
    if (a->cr0_val != b->cr0_val)
        diff("CR0", a->cr0_val, b->cr0_val);
    if (a->abrt != b->abrt)
        diff("abrt", a->abrt, b->abrt);
    if (a->npxc != b->npxc)
        diff("FCW", a->npxc, b->npxc);
    if (a->npxs != b->npxs)
        diff("FSW", a->npxs, b->npxs);
    if (a->top != b->top)
        diff("TOP", a->top, b->top);
    for (int i = 0; i < 8; i++) {
        if (a->tag[i] != b->tag[i]) {
            snprintf(name, sizeof(name), "tag[%d]", i);
            diff(name, a->tag[i], b->tag[i]);
        }
        if (a->st[i] != b->st[i]) {
            snprintf(name, sizeof(name), "ST[%d]", i);
            diff(name, a->st[i], b->st[i]);
        }
        if (a->mm[i] != b->mm[i]) {
            snprintf(name, sizeof(name), "MM%d", i);
            diff(name, a->mm[i], b->mm[i]);
        }
    }
    if (a->ismmx != b->ismmx)
        diff("ismmx", a->ismmx, b->ismmx);
    if (a->vector != b->vector)
        diff("fault", a->vector, b->vector);
    if (a->tos != b->tos)
        diff("fault.tos", a->tos, b->tos);
    for (int i = 0; i < HARNESS_PAGE; i++) {
        if (a->data[i] != b->data[i]) {
            snprintf(name, sizeof(name), "data+%03x", i);
            diff(name, a->data[i], b->data[i]);
        }
    }
    if (a->stray_pages != b->stray_pages)
        diff("ram.pages", a->stray_pages, b->stray_pages);
    else if (a->stray_hash != b->stray_hash)
        diff("ram.hash", a->stray_hash, b->stray_hash);
    return diff_lines;
}

static void
dump_state(const harness_state_t *s)
{
    printf("    %s EIP=%08x FLAGS=%04x fault=%08x tos=%08x tsc=%" PRIu64 "\n", status_names[s->status], s->pc, s->flags, s->vector, s->tos, s->tsc_delta);
    printf("    EAX=%08x ECX=%08x EDX=%08x EBX=%08x ESP=%08x EBP=%08x ESI=%08x EDI=%08x\n",
           s->regs[0], s->regs[1], s->regs[2], s->regs[3], s->regs[4], s->regs[5], s->regs[6], s->regs[7]);
    printf("    FCW=%04x FSW=%04x TOP=%d tags=%d%d%d%d%d%d%d%d\n", s->npxc, s->npxs, s->top,
           s->tag[0], s->tag[1], s->tag[2], s->tag[3], s->tag[4], s->tag[5], s->tag[6], s->tag[7]);
    for (int i = 0; i < 8; i++) {
        double d;
        memcpy(&d, &s->st[i], 8);
        printf("    ST[%d]=%016" PRIx64 " (%.17g)  MM%d=%016" PRIx64 "\n", i, s->st[i], d, i, s->mm[i]);
    }
    if (s->stray_pages)
        printf("    stores outside the data page: %u page(s), first at %08x, hash %016" PRIx64 "\n", s->stray_pages, s->stray_first, s->stray_hash);
}

/* Same test as compare(), without the printing. */
static int
compare_silent(const harness_state_t *a, const harness_state_t *b)
{
    return a->status != b->status || memcmp(a->regs, b->regs, sizeof(a->regs)) || a->flags != b->flags
        || a->eflags != b->eflags || memcmp(a->seg, b->seg, sizeof(a->seg)) || memcmp(a->base, b->base, sizeof(a->base))
        || a->pc != b->pc || a->cr0_val != b->cr0_val || a->abrt != b->abrt || a->npxc != b->npxc || a->npxs != b->npxs
        || a->top != b->top || memcmp(a->tag, b->tag, sizeof(a->tag)) || memcmp(a->st, b->st, sizeof(a->st))
        || memcmp(a->mm, b->mm, sizeof(a->mm)) || a->ismmx != b->ismmx || a->vector != b->vector || a->tos != b->tos
        || memcmp(a->data, b->data, sizeof(a->data)) || a->stray_pages != b->stray_pages || a->stray_hash != b->stray_hash;
}

/* ---- a case ---------------------------------------------------------- */

static int passes = 3;
static int dump   = 0;
static int quiet  = 0;
static int truncate_to = -1; /* cut a generated body to this many bytes */

/* Returns 0 when every run agrees, else a bit set: 1 state, 2 timing. */
static int
run_case(const harness_case_t *c)
{
    harness_state_t st[4];
    int             nruns = 1 + passes;
    int             result = 0;

    if (nruns > 4)
        nruns = 4;
    cur_case = c;
    build_code(c);
    if (harness_verbose) {
        fprintf(stderr, "harness: case %s\n", c->label);
        dump_case_hex(c);
    }

    /* Fresh block cache for every case: the code page gets new bytes
       without the CPU writing them, so nothing marks the old blocks. */
    step("codegen_reset");
    codegen_reset();

    step(run_names[0]);
    load_case(c, 1);
    step("run interpreter");
    st[0].status = run_until_halt(0);
    capture(&st[0], st[0].status);

    for (int r = 1; r < nruns; r++) {
        step(run_names[r]);
        load_case(c, r == 1);
        step("run recompiler");
        st[r].status = run_until_halt(1);
        capture(&st[r], st[r].status);
    }
    step("report");

    if (quiet) {
        int bad = 0;
        for (int r = 1; r < nruns && !bad; r++)
            bad = compare_silent(&st[0], &st[r]) || (r >= 2 && st[r].tsc_delta != st[1].tsc_delta);
        if (!bad) {
            cur_case = NULL;
            return 0;
        }
    }

    printf("%s: ", c->label);
    for (int r = 1; r < nruns; r++) {
        if (compare(&st[0], &st[r])) {
            printf("\n  STATE %s != %s (%d fields)\n", run_names[0], run_names[r], diff_lines);
            result |= 1;
        }
    }
    for (int r = 2; r < nruns; r++) {
        if (st[r].tsc_delta != st[1].tsc_delta) {
            printf("\n  TIMING %s %" PRIu64 " != %s %" PRIu64 "\n", run_names[1], st[1].tsc_delta, run_names[r], st[r].tsc_delta);
            result |= 2;
        }
    }
    if (!result)
        printf("ok (%s", status_names[st[0].status]);
    else
        printf("  summary: (%s", status_names[st[0].status]);
    if (st[0].vector != 0xffffffff)
        printf(", fault %02x", st[0].vector);
    printf("; cycles interpreter %" PRIu64 ", recompiler %" PRIu64 ")\n", st[0].tsc_delta, st[1].tsc_delta);

    if (result || dump) {
        for (int r = 0; r < nruns; r++) {
            printf("  %s:\n", run_names[r]);
            dump_state(&st[r]);
        }
        dump_case_hex(c);
    }
    cur_case = NULL;
    return result;
}

/* ---- case sources ---------------------------------------------------- */

static void
default_case(harness_case_t *c)
{
    memset(c, 0, sizeof(*c));
    c->regs[3] = HARNESS_DATA + 0x800; /* EBX into the data page */
    c->regs[4] = HARNESS_ESP0;
    c->regs[6] = HARNESS_DATA;         /* ESI, EDI at the data page */
    c->regs[7] = HARNESS_DATA + 0x400;
    c->eflags  = 0x0002;
    c->npxc    = 0x037f;
    for (int i = 0; i < HARNESS_PAGE; i++)
        c->data[i] = (uint8_t) (i * 7 + (i >> 8));
    strcpy(c->label, "hand");
}

static int
parse_hex(const char *s, uint8_t *out, int max)
{
    int n = 0;

    while (*s) {
        unsigned v;
        if (*s == ' ' || *s == ',') {
            s++;
            continue;
        }
        if (n >= max || sscanf(s, "%2x", &v) != 1)
            return -1;
        out[n++] = v;
        s += 2;
    }
    return n;
}

static void
usage(void)
{
    fprintf(stderr,
            "cpu_harness: run a code blob under the interpreter and the recompiler and diff the state\n"
            "  --hex <bytes>        body as hex (32-bit flat; EBX, ESI, EDI point into the data page)\n"
            "  --bin <file>         body from a file\n"
            "  --seed <n>           one generated case\n"
            "  --seeds <a>-<b>      generated cases a..b\n"
            "  --class <list>       generator classes, comma separated (%s), default all\n"
            "  --mode16             16-bit code and stack segments\n"
            "  --softfloat          softfloat FPU (the compiled x87 tables are bypassed)\n"
            "  --nolink             block linking off\n"
            "  --passes <n>         recompiler passes after the interpreter run (1-3, default 3)\n"
            "  --dump               print every run's final state\n"
            "  --quiet              print only the cases with differences, and the total\n"
            "  --truncate <n>       cut each generated body to n bytes (bisecting a case)\n"
            "  --verbose            core log lines to stderr\n",
            harness_gen_class_names());
}

int
main(int argc, char **argv)
{
    harness_case_t *c = malloc(sizeof(*c));
    uint64_t        seed_a = 0, seed_b = 0;
    int             have_seeds = 0;
    int             have_body  = 0;
    int             mode16     = 0;
    int             softfloat  = 0;
    int             nolink     = 0;
    uint32_t        classes    = GEN_ALL;
    int             failures   = 0;
    int             total      = 0;

    default_case(c);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hex") && i + 1 < argc) {
            c->body_len = parse_hex(argv[++i], c->body, HARNESS_BODY_MAX);
            if (c->body_len < 0) {
                fprintf(stderr, "bad hex body\n");
                return 2;
            }
            have_body = 1;
        } else if (!strcmp(argv[i], "--bin") && i + 1 < argc) {
            FILE *f = fopen(argv[++i], "rb");
            if (!f) {
                perror(argv[i]);
                return 2;
            }
            c->body_len = (int) fread(c->body, 1, HARNESS_BODY_MAX, f);
            fclose(f);
            snprintf(c->label, sizeof(c->label), "%s", argv[i]);
            have_body = 1;
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed_a = seed_b = strtoull(argv[++i], NULL, 0);
            have_seeds = 1;
        } else if (!strcmp(argv[i], "--seeds") && i + 1 < argc) {
            if (sscanf(argv[++i], "%" SCNu64 "-%" SCNu64, &seed_a, &seed_b) != 2) {
                fprintf(stderr, "bad seed range\n");
                return 2;
            }
            have_seeds = 1;
        } else if (!strcmp(argv[i], "--class") && i + 1 < argc) {
            if (!harness_gen_classes(argv[++i], &classes)) {
                fprintf(stderr, "unknown class in %s (known: %s)\n", argv[i], harness_gen_class_names());
                return 2;
            }
        } else if (!strcmp(argv[i], "--mode16")) {
            mode16 = 1;
        } else if (!strcmp(argv[i], "--softfloat")) {
            softfloat = 1;
        } else if (!strcmp(argv[i], "--nolink")) {
            nolink = 1;
        } else if (!strcmp(argv[i], "--passes") && i + 1 < argc) {
            passes = atoi(argv[++i]);
            if (passes < 1 || passes > 3) {
                fprintf(stderr, "passes must be 1..3\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--dump")) {
            dump = 1;
        } else if (!strcmp(argv[i], "--quiet")) {
            quiet = 1;
        } else if (!strcmp(argv[i], "--truncate") && i + 1 < argc) {
            truncate_to = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--verbose")) {
            harness_verbose = 1;
        } else {
            usage();
            return 2;
        }
    }
    if (!have_body && !have_seeds) {
        usage();
        return 2;
    }

    setup_machine(softfloat);
    if (nolink)
        cpu_dyn_block_link = 0;

    if (have_body) {
        c->mode16 = mode16;
        total++;
        if (run_case(c))
            failures++;
    }
    if (have_seeds) {
        for (uint64_t s = seed_a; s <= seed_b; s++) {
            harness_gen_case(c, s, classes);
            c->mode16 = mode16;
            if (truncate_to >= 0 && truncate_to < c->body_len)
                c->body_len = truncate_to;
            total++;
            if (run_case(c))
                failures++;
        }
    }

    printf("%d case%s, %d with differences\n", total, total == 1 ? "" : "s", failures);
    free(c);
    return failures ? 1 : 0;
}
