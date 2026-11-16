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
 *          recompiler until a run compiles nothing (its mark pass, its
 *          compile passes, the compiled code, and the compiled code again
 *          with the links the first compiled run made), and diffs the
 *          resulting state.
 *
 *          The guest is a flat 32-bit (or 16-bit) protected-mode
 *          environment built straight into RAM: a GDT, an IDT whose
 *          every gate lands on a handler that records the vector and
 *          halts, a stack page, a data page and a code page. The blob
 *          sits between a prelude and an epilogue that bracket it with
 *          RDTSC (an OUT to the harness's TSC device on a CPU without it)
 *          and end in HLT; the driver runs cycle slices until the CPU
 *          halts.
 *
 *          Architectural state (registers, flags, segments, x87, MMX, the
 *          data page, the fault record) must agree between every run.
 *          The RDTSC delta must agree between the two compiled runs; the
 *          interpreter, the mark pass and the compile passes charge the
 *          interpreter's per-instruction cycles for what they interpret,
 *          and their deltas are only reported.
 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#    include <windows.h>
#    include <io.h>
#else
#    include <unistd.h>
#endif

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
#include "codegen_backend.h"

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
    uint16_t st_exp[8]; /* softfloat: sign and exponent of each register; st holds the significand */
    uint64_t mm[8];
    uint8_t  ismmx;
    uint32_t vector;
    uint32_t tos;
    uint64_t tsc_delta;
    uint32_t stray_pages; /* pages written outside the ones the case owns */
    uint32_t stray_first; /* the lowest of them */
    uint64_t stray_hash;  /* over their addresses and contents */
    uint32_t code_bytes;  /* code page bytes the run left changed */
    uint32_t code_first;  /* the lowest of them */
    uint64_t code_hash;   /* over the code page as the run left it */
    uint8_t  data[HARNESS_PAGE];
} harness_state_t;

/* The recompiler marks a block on its first visit (interpreted), compiles
   it on the second (interpreted while the code is generated) and runs the
   compiled code from the third. Run names: "interpreter", "mark pass",
   "compile pass" (numbered from the second), "compiled", "compiled 2". */
#define MAX_RUNS 64
static char run_name[MAX_RUNS + 1][24];

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

/* --list: the process's own stderr, kept while stderr is a case's; the
   entry being run, from 1, and its hex file. */
static FILE       *list_log;
static int         list_entry;
static const char *list_hex_name;

void
harness_fatal(const char *msg)
{
    fprintf(stderr, "FATAL from the core during %s: %s\n", cur_phase, msg);
    if (cur_case) {
        fprintf(stderr, "  case: %s\n", cur_case->label);
        dump_case_hex(cur_case);
    }
    /* The entry's files get their names when it has finished: this one and
       the ones after it get none (list_abandon() removes its work files as
       the process exits), and whoever reads the list's stderr is told where
       it stopped. */
    if (list_log) {
        fprintf(list_log, "FATAL from the core during %s: %s\n", cur_phase, msg);
        fprintf(list_log, "list entry %d (%s) was running: it and the entries after it have no output files\n", list_entry, list_hex_name);
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

/* Without RDTSC the TSC device takes the times: OUT imm8, AL changes no
   register and no flag, so the prelude and the epilogue need nothing else. */
static int
emit_prelude(uint8_t *p, int mode16)
{
    int n = 0;

    if (!harness_model.rdtsc) {
        p[n++] = 0xe6; /* out HARNESS_PORT_TSC_START, al */
        p[n++] = HARNESS_PORT_TSC_START;
        return n;
    }
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

    if (!harness_model.rdtsc) {
        p[n++] = 0xe6; /* out HARNESS_PORT_TSC_END, al */
        p[n++] = HARNESS_PORT_TSC_END;
        p[n++] = 0xf4; /* hlt */
        return n;
    }
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

/* The TSC device's OUT: the time as RDTSC reads it, the recompiler's block
   cycles so far first (opRDTSC), and the record page written as the RDTSC
   prelude and epilogue write it. */
void
harness_tsc_port(uint16_t port)
{
    uint64_t t;

    if (cpu_use_dynarec)
        update_tsc();
    t = tsc;
    if (port == HARNESS_PORT_TSC_END) {
        t -= get32(HARNESS_RECORD + REC_TSC_START) | ((uint64_t) get32(HARNESS_RECORD + REC_TSC_START + 4) << 32);
        mem_writel_phys(HARNESS_RECORD + REC_TSC_DELTA, (uint32_t) t);
        mem_writel_phys(HARNESS_RECORD + REC_TSC_DELTA + 4, (uint32_t) (t >> 32));
    } else {
        mem_writel_phys(HARNESS_RECORD + REC_TSC_START, (uint32_t) t);
        mem_writel_phys(HARNESS_RECORD + REC_TSC_START + 4, (uint32_t) (t >> 32));
    }
}

/* The 386 loop option: every run of a case goes through exec386_2386(), the
   loop a 286 or 386 runs with the recompiler off, with its own memory and
   segment code (mmu_2386.c, the OPS_286_386 builds). No run is compiled, so
   the runs only show that the loop repeats itself; the state they print is
   what a build is compared on. */
static int loop_2386;

/* The SMM configuration as reset left it, put back before every run: a run
   that ends inside SMM would otherwise start the next one there. */
static uint8_t  ref_ccr1;
static cyrix_t  ref_cyrix;
static gxm_t    ref_gxm;
static uint32_t ref_smbase;

/* The SMI device's OUT. A Cyrix CPU takes SMI# only once its configuration
   registers enable it and give SMM its memory (ARR3) and header address: the
   board's BIOS writes them through ports 22h and 23h, which are not here, so
   the same values are set directly, 64 KB of SMM memory at 30000h. */
void
harness_smi(void)
{
    if (is_cxsmm) {
        ccr1              = (ccr1 & ~CCR1_SMAC) | CCR1_USE_SMI | (is_cx6x86 ? CCR1_SM3 : 0);
        cyrix.arr[3].base = HARNESS_SMM_ENTRY_CYRIX;
        cyrix.arr[3].size = 0x00010000;
        cyrix.smhr        = 0x00040000 | SMHR_VALID;
    }
    smi_raise();
}

/* IRQ 13, which FERR# raises with CR0.NE clear (a 386): served at once, as
   a write to port F0h serves it, whose board latch asserts IGNNE# while
   FERR# is up (cpu.c, cpu_write). There is no interrupt controller here to
   deliver it, and the waiting instruction FERR# holds would be restarted
   forever without a cycle charged, so the slice would never end. */
void
harness_irq13(void)
{
    if (fpu_ferr)
        fpu_ignne = 1;
}

/* ---- machine --------------------------------------------------------- */

static uint8_t *ram_ref; /* RAM as set up, before any case ran */
/* CR2, the debug registers and the MSRs as reset left them: a case can write
   them, and load_case() puts them back. With the MSRs go the values a WRMSR
   works out from them (the WinChip and C3 feature control registers set the
   CPUID enable, the feature bits and which CR4 bits stick). */
static uint32_t ref_cr2;
static uint32_t ref_dr[8];
static msr_t    ref_msr;
static int      ref_cpuid;
static uint32_t ref_cpu_features;
static uint64_t ref_cr4_mask;
static uint64_t ref_cr4_ignore;

/* What a case changes that the hard reset leaves as it finds it, and a new
   process has as setup_machine() left it: the CPU state block, whose reset
   keeps the fields no RESET defines; the descriptor table registers; and
   what the interpreter, the far-transfer code and the page walk keep
   between instructions. reset_machine() puts each back by value. */
typedef struct fresh_t {
    void  *obj;
    size_t size;
    void  *copy;
} fresh_t;

#define FRESH(x) { &(x), sizeof(x), NULL }
static fresh_t fresh[] = {
    FRESH(cpu_state),
    FRESH(gdt),
    FRESH(ldt),
    FRESH(idt),
    FRESH(tr),
    FRESH(abrt_error),
    FRESH(addr64),
    FRESH(cgate16),
    FRESH(cgate32),
    FRESH(cgate_task),
    FRESH(intgatesize),
    FRESH(cpu_block_end),
    FRESH(eal_r),
    FRESH(eal_w),
    FRESH(easeg),
    FRESH(get_phys_virt),
    FRESH(mem_logical_addr),
    FRESH(oddeven),
    FRESH(oldcs),
    FRESH(oldss),
    FRESH(opcode),
    FRESH(optype),
    FRESH(pae_pdpte),
    FRESH(prefetch_prefixes),
    FRESH(rep_op),
    FRESH(rf_flag_no_clear),
    FRESH(rmdat)
};
#undef FRESH
#define FRESH_NR ((int) (sizeof(fresh) / sizeof(fresh[0])))

static void
step(const char *what)
{
    cur_phase = what;
    if (harness_verbose)
        fprintf(stderr, "harness: %s\n", what);
}

/* The machine: the 600 MHz Deschutes and the 486 debug registers option
   off, unless --cpu or --dbg486 say otherwise. The FPU is the CPU's own
   where its table entry has one, else the last coprocessor the entry lists
   (the 387 on a 386), unless --fpu-type says otherwise. */
static char machine_family[64]   = "pentium2_deschutes";
static char machine_cpu_name[32] = "600";
static int  machine_fpu_type     = -1;
static int  machine_dbg486       = 0;
static int  machine_ram_kb       = HARNESS_RAM_KB;

harness_model_t harness_model;

/* CPUID function 1's EDX as the CPU answers it, 0 for a CPU without CPUID.
   A CPUID can set MSR state (the Pentium II's BIOS signature): the MSRs
   and the registers are put back. */
static uint32_t
model_cpuid_edx(void)
{
    x86reg   regs[4];
    msr_t    msr_before = msr;
    uint32_t edx        = 0;

    if (!CPUID)
        return 0;
    memcpy(regs, cpu_state.regs, sizeof(regs));
    EAX = 0;
    cpu_CPUID();
    if (EAX >= 1) {
        EAX = 1;
        ECX = 0;
        cpu_CPUID();
        edx = EDX;
    }
    memcpy(cpu_state.regs, regs, sizeof(regs));
    msr = msr_before;
    return edx;
}

/* What the instruction sets in the CPUs' books add, by the CPU table's
   type and features. RDTSC and MMX as the interpreter tests them. BSWAP
   is "not available on Intel386 DX or SX microprocessors" (Intel486 PRM
   1992, 3.11); it is drawn from the 486 class on where the part's own book
   lists it (Intel486, Am486, Pentium and its OverDrive's "true Pentium
   processor core", 6x86, 6x86MX, K5, K6, GX1, Pentium Pro and later),
   and not for the Cx486S, Cx486DX, STPC, Cx5x86, WinChip and Cyrix III,
   whose books here do not list their instructions. CMOVcc,
   FCMOVcc and FCOMI from CPUID's CMOV flag, which "supports the CMOVcc
   instruction and, if the FPU feature flag (bit 0) is also set, supports
   the FCMOVcc and FCOMI instructions" (SDM 1999 vol. 2, CPUID). The GXm
   sets the CMOV flag for the integer moves only: its extended CPUID gives
   "FPU Conditional Move Instruction" as not supported (GXm data book,
   Table 9-23), so it gets no FCMOVcc or FCOMI. FUCOM(P)(P), FPREM1, FSIN,
   FCOS and FSINCOS "do not exist" on the 8087 and 80287 (80387 PRM 1987,
   appendix D); the 287XL "supports all 80387 instructions" (Intel287 XL/XLT
   data sheet 1992). */
static void
model_probe(void)
{
    const int      type = cpu_s->cpu_type;
    const uint32_t edx  = model_cpuid_edx();

    harness_model.rdtsc   = !!cpu_has_feature(CPU_FEATURE_RDTSC);
    harness_model.mmx     = !!cpu_has_feature(CPU_FEATURE_MMX);
    harness_model.bswap   = (type >= CPU_i486SX) && (type != CPU_Cx486S) && (type != CPU_Cx486DX) && (type != CPU_STPC) &&
                            (type != CPU_Cx5x86) && (type != CPU_WINCHIP) && (type != CPU_WINCHIP2) && (type != CPU_CYRIX3S);
    harness_model.cmov    = !!(edx & (1 << 15)); /* CMOV */
    harness_model.fcmov   = harness_model.cmov && (edx & 1) && hasfpu && (type != CPU_CxGX1); /* and FPU */
    harness_model.x87     = hasfpu;
    harness_model.x87_387 = hasfpu && (fpu_type >= FPU_287XL);
}

/* What mem_reset() gives a new process, put back in place: mem_reset()
   itself maps RAM and allocates the page table anew each time it runs. RAM
   as set up; the page table's dirty and code masks and its eviction list
   empty (its block lists are codegen_reset()'s); the cache-as-RAM ranges
   and the A20 gate as mem_reset() leaves them. The mappings are not a
   case's to change. */
extern int purgeable_page_count; /* mem.c's, in no header */

static void
restore_memory(void)
{
    const size_t mask_bytes = ((size_t) mem_size * 1024) / 8;

    step("memory");
    memcpy(ram, ram_ref, mem_size * 1024);
    memset(page_ff, 0xff, sizeof(page_ff));
    for (uint32_t i = 0; i < pages_sz; i++) {
        pages[i].code_present_mask = 0;
        pages[i].dirty_mask        = 0;
        pages[i].evict_prev        = EVICT_NOT_IN_LIST;
        pages[i].evict_next        = 0;
    }
    memset(byte_dirty_mask, 0, mask_bytes);
    memset(byte_code_present_mask, 0, mask_bytes);
    purgable_page_list_head = 0;
    purgeable_page_count    = 0;
    cpu_car_reset();
    mem_a20_init();
}

/* The machine's hard reset in the emulator's order (pc_reset_hard_init()
   and machine_init()): the timers, the CPU, the memory, then the CPU's
   RESET. setup_machine() runs it once, with the memory's allocation and the
   recompiler's one-time init where 86box.c has them; reset_machine() runs
   it again, the memory put back in place. */
static void
hard_reset_machine(int first)
{
    step("timer_init");
    timer_init();
    step("cpu_set");
    cpu_set();
    if (first) {
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
    } else
        restore_memory();
    step("resetx86");
    resetx86();
}

static void
setup_machine(int softfloat, int fast_fpu_env)
{
    const CPU *cpus;
    int        n;

    mem_size = machine_ram_kb;
    cpu_f    = cpu_get_family(machine_family);
    if (cpu_f == NULL) {
        fprintf(stderr, "no %s family in the CPU table\n", machine_family);
        exit(2);
    }
    cpus = cpu_f->cpus;
    for (n = 0; cpus[n].name[0]; n++) {
        if (!strcmp(cpus[n].name, machine_cpu_name))
            break;
    }
    if (!cpus[n].name[0]) {
        fprintf(stderr, "no CPU %s in the %s family\n", machine_cpu_name, machine_family);
        exit(2);
    }
    cpu                = n;
    if (machine_fpu_type < 0) {
        const FPU *fpus = cpus[n].fpus;

        machine_fpu_type = FPU_NONE;
        for (int i = 0; fpus[i].name; i++) {
            machine_fpu_type = fpus[i].type;
            if (machine_fpu_type == FPU_INTERNAL)
                break;
        }
    }
    fpu_type           = machine_fpu_type;
    cpu_debug_regs_486 = machine_dbg486;
    fpu_softfloat      = softfloat;
    cpu_use_dynarec    = !loop_2386;
    /* The interpreter always records the x87 instruction and data pointers;
       the recompiler only does so under the accurate FPU environment option,
       so the comparison runs with it on unless the fast mode is under test. */
    cpu_dyn_accurate_fpu_env = !fast_fpu_env;

    step("mem_init");
    mem_init();
    hard_reset_machine(1);
    if (loop_2386 && (cpu_exec != exec386_2386)) {
        fprintf(stderr, "--loop2386: %s:%s does not run the 386 loop\n", machine_family, machine_cpu_name);
        exit(2);
    }
    ref_ccr1   = ccr1;
    ref_cyrix  = cyrix;
    ref_gxm    = gxm;
    ref_smbase = smbase;
    model_probe();

    step("tables");
    build_tables();

    /* The reference every page the cases do not own is held to. */
    ram_ref = malloc(mem_size * 1024);
    if (ram_ref == NULL)
        harness_fatal("no memory for the RAM reference");
    memcpy(ram_ref, ram, mem_size * 1024);

    ref_cr2 = cr2;
    memcpy(ref_dr, dr, sizeof(ref_dr));
    ref_msr = msr;
    ref_cpuid        = CPUID;
    ref_cpu_features = cpu_features;
    ref_cr4_mask     = cpu_CR4_mask;
    ref_cr4_ignore   = cpu_CR4_ignore;

    for (int i = 0; i < FRESH_NR; i++) {
        fresh[i].copy = malloc(fresh[i].size);
        if (fresh[i].copy == NULL)
            harness_fatal("no memory for the machine reference");
        memcpy(fresh[i].copy, fresh[i].obj, fresh[i].size);
    }
}

/* The machine as setup_machine() left it, for a --list case that is not the
   process's first. The blocks go first, with the page table still as they
   left it. The recompiler's own state past
   codegen_reset() (where a block's host code lands, its link generation,
   the block version pool) is not the machine's and stays. */
static void
reset_machine(void)
{
    cur_phase = "machine reset";
    codegen_reset();
    /* As setup had it: cpu_set() picks the opcode tables by it. */
    cpu_use_dynarec = !loop_2386;
    hard_reset_machine(0);
    /* The 386's prefetch queue, and what the Pentium and WinChip 2 timing
       models carry from one block to the next: no reset empties them. */
    prefetch_flush();
    if (codegen_timing_load)
        codegen_timing_load(NULL);
    for (int i = 0; i < FRESH_NR; i++)
        memcpy(fresh[i].obj, fresh[i].copy, fresh[i].size);
}

/* ---- case state ------------------------------------------------------ */

static uint8_t  code_image[HARNESS_PAGE];
static int      code_len;
static uint32_t body_start; /* the body's first byte, linear */
static uint32_t body_end;   /* the epilogue's first byte */

static void
build_code(const harness_case_t *c)
{
    int n = 0;

    memset(code_image, 0xf4, sizeof(code_image)); /* HLT past the end */
    n += emit_prelude(code_image + n, c->mode16);
    body_start = HARNESS_CODE + n;
    memcpy(code_image + n, c->body, c->body_len);
    n += c->body_len;
    body_end = HARNESS_CODE + n;
    n += emit_epilogue(code_image + n, c->mode16);
    code_len = n;
}

/* A case's double as an 80-bit register, exactly. A NaN is built by hand:
   f64_to_extF80 would quiet a signaling one. */
static floatx80
widen_double(double d)
{
    struct softfloat_status_t status;
    uint64_t                  b;
    floatx80                  r;

    memcpy(&b, &d, 8);
    if (((b >> 52) & 0x7ff) == 0x7ff && (b & 0x000fffffffffffffULL)) {
        r.signExp = ((b >> 48) & 0x8000) | 0x7fff;
        r.signif  = 0x8000000000000000ULL | ((b & 0x000fffffffffffffULL) << 11);
        return r;
    }
    memset(&status, 0, sizeof(status));
    return f64_to_extF80(b, &status);
}

/* The softfloat FPU keeps its own state (fpu_state) and its own registers,
   whose significands are the MMX registers. The case's control word goes
   in as FLDCW would leave it. A valid register holds the case's double; an
   empty one holds the case's MMX value as MMX code leaves it (exponent all
   ones), the one place both fit in one register. Tags as FRSTOR sets them
   from the register contents. */
static void
load_softfloat(const harness_case_t *c)
{
    fpu_state.cwd = (c->npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
    fpu_state.swd = (c->top & 7) << 11;
    fpu_state.tos = c->top & 7;
    fpu_state.tag = 0;
    for (int i = 0; i < 8; i++) {
        if (c->tag[i] == TAG_EMPTY) {
            fpu_state.st_space[i].signExp = 0xffff;
            fpu_state.st_space[i].signif  = c->mm[i];
            fpu_state.tag |= X87_TAG_EMPTY << (i * 2);
        } else {
            fpu_state.st_space[i] = widen_double(c->st[i]);
            fpu_state.tag |= FPU_tagof(fpu_state.st_space[i]) << (i * 2);
        }
    }
    fpu_state.foo = 0;
    fpu_state.fip = 0;
    fpu_state.fdp = 0;
    fpu_state.fcs = 0;
    fpu_state.fds = 0;
}

/* load_case() finds the blocks handed out since codegen_reset() from two
   properties of the block cache: the reset's free list hands the blocks out
   from the last one down and takes a freed block back on top, so they are
   the numbers from some block up to the last; and a block handed out keeps a
   dirty_mask pointer for good, which the reset clears. More blocks than
   this, and it walks every page and block instead. */
#define LOAD_CASE_BLOCKS_MAX 256

/* Set for a case: load_case() then also walks every page and every block,
   and stops the process if the short walk left one of them undone. */
static int load_case_audit;

static int
page_nr_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *) a;
    uint32_t y = *(const uint32_t *) b;

    return (x > y) - (x < y);
}

static void
load_case(const harness_case_t *c, int with_code)
{
    uint32_t low;

    cur_phase = "state load";

    if (with_code)
        memcpy(ram + HARNESS_CODE, code_image, HARNESS_PAGE);
    memcpy(ram + HARNESS_DATA, c->data, HARNESS_PAGE);
    memset(ram + HARNESS_STACK, 0, HARNESS_PAGE);
    memset(ram + HARNESS_RECORD, 0, HARNESS_PAGE);
    /* Put back without the CPU writing them: a block compiled from code a
       run wrote there would otherwise survive the reload. */
    mem_invalidate_range(HARNESS_DATA, HARNESS_DATA);
    mem_invalidate_range(HARNESS_STACK, HARNESS_STACK);
    mem_invalidate_range(HARNESS_RECORD, HARNESS_RECORD);
    /* The SMC thrash counts and cooldown deadlines as a reset machine has
       them, on every block: a case may run code anywhere, and the strikes
       earned by earlier runs would send a block to the interpreter in this
       one. The blocks over the pages put back above, and over those the last
       capture put back, are dropped first: the run would drop them as it
       reached them and count their strikes after the clear. Every page is
       flushed before any block is cleared.
       Only the blocks handed out since the case's codegen_reset() and the
       pages they name are visited, the pages in ascending order: the reset
       leaves every page list empty and every count zero, and no other page
       or block can have changed. */
    low = BLOCK_SIZE;
    while (low > 1 && codeblock[low - 1].dirty_mask)
        low--;
    if (BLOCK_SIZE - low > LOAD_CASE_BLOCKS_MAX) {
        for (uint32_t i = 0; i < pages_sz; i++) {
            page_t *page = &pages[i];

            if ((page->block || page->block_2) && page->dirty_mask)
                codegen_check_flush(page, page->dirty_mask, i << 12);
        }
        for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
            codeblock[i].smc_strikes        = 0;
            codeblock[i].smc_cooldown_until = 0;
        }
    } else {
        uint32_t page_nr[2 * LOAD_CASE_BLOCKS_MAX];
        int      nr = 0;

        /* A block that left its page lists keeps its addresses: it adds a
           page whose lists are tested like any other's. */
        for (uint32_t i = low; i < BLOCK_SIZE; i++) {
            if ((codeblock[i].phys >> 12) < pages_sz)
                page_nr[nr++] = codeblock[i].phys >> 12;
            if ((codeblock[i].phys_2 >> 12) < pages_sz)
                page_nr[nr++] = codeblock[i].phys_2 >> 12;
        }
        qsort(page_nr, nr, sizeof(page_nr[0]), page_nr_cmp);
        for (int j = 0; j < nr; j++) {
            page_t *page = &pages[page_nr[j]];

            if (j && (page_nr[j] == page_nr[j - 1]))
                continue;
            if ((page->block || page->block_2) && page->dirty_mask)
                codegen_check_flush(page, page->dirty_mask, page_nr[j] << 12);
        }
        for (uint32_t i = low; i < BLOCK_SIZE; i++) {
            codeblock[i].smc_strikes        = 0;
            codeblock[i].smc_cooldown_until = 0;
        }
        if (load_case_audit) {
            for (uint32_t i = 0; i < pages_sz; i++) {
                const page_t *page = &pages[i];

                if ((page->block || page->block_2) && page->dirty_mask)
                    harness_fatal("load_case: a page outside the visited ones has blocks to flush");
            }
            for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
                if (codeblock[i].smc_strikes || codeblock[i].smc_cooldown_until)
                    harness_fatal("load_case: a block outside the visited ones has SMC strikes");
            }
        }
    }
    put32(HARNESS_RECORD + REC_VECTOR, 0xffffffff);
    harness_ram_writer_addr = 0;

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
       With NE clear it is FERR# and IRQ13 (harness_irq13). CD and NW, set
       by reset, cleared: the dynarec loop interprets every block while CD
       is set and would never run compiled code. ET as reset sets it; MP, EM, TS,
       WP, AM and PG clear. The whole register is set, not merged: a case
       that writes CR0 must not hand its bits to the next run. The cache
       flag and the wait states follow CD as they do after a MOV CR0. A
       386 has no NE: it keeps only what a MOV CR0 would keep. */
    cr0 = cpu_cr0_write_value(0x00000031);
    /* CR2, the debug registers and the MSRs as reset left them, before the
       cache-as-RAM state below is worked out from the MTRRs. The armed
       breakpoint flag follows DR7. */
    cr2 = ref_cr2;
    memcpy(dr, ref_dr, sizeof(ref_dr));
    msr = ref_msr;
    CPUID          = ref_cpuid;
    cpu_features   = ref_cpu_features;
    cpu_CR4_mask   = ref_cr4_mask;
    cpu_CR4_ignore = ref_cr4_ignore;
    cpu_dr7_update();
    cpu_cache_int_enabled = hascache && !(cr0 & (1 << 30));
    if (hascache)
        cpu_update_waitstates();
    cpu_car_update();
    cr4 = 0;
    cr3 = 0;
    /* The segment loads below read the GDT through the lookup tables: a
       translation the previous run left there with paging on would still
       be used with PG now clear. */
    flushmmucache();

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
    in_smm      = 0;
    smi_line    = 0;
    smi_latched = 0;
    smm_in_hlt  = 0;
    ccr1        = ref_ccr1;
    cyrix       = ref_cyrix;
    gxm         = ref_gxm;
    smbase      = ref_smbase;

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
    if (fpu_softfloat)
        load_softfloat(c);
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

/* The code page is not reloaded between the recompiler runs, so that their
   blocks survive. A byte a run changed there is hashed into the state and
   written back as a CPU write would write it, so the recompiler's dirty
   tracking drops the blocks over it and a reload of the page is not needed. */
static void
capture_code_page(harness_state_t *s)
{
    s->code_bytes = 0;
    s->code_first = 0;
    s->code_hash  = 0xcbf29ce484222325ull;
    if (!memcmp(ram + HARNESS_CODE, code_image, HARNESS_PAGE))
        return;
    for (int i = 0; i < HARNESS_PAGE; i++)
        s->code_hash = (s->code_hash ^ ram[HARNESS_CODE + i]) * 0x100000001b3ull;
    for (int i = 0; i < HARNESS_PAGE; i++) {
        uint32_t a = HARNESS_CODE + i;

        if (ram[a] == code_image[i])
            continue;
        if (!s->code_bytes)
            s->code_first = a;
        s->code_bytes++;
        mem_write_ramb_page(a, code_image[i], &pages[a >> 12]);
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
    /* The softfloat FPU leaves the fields above as load_case() set them:
       its own state is what the runs are compared on. The MMX registers
       are the registers' significands. */
    if (fpu_softfloat) {
        s->npxc = fpu_state.cwd;
        s->npxs = i387_get_status_word();
        s->top  = fpu_state.tos;
        for (int i = 0; i < 8; i++) {
            s->tag[i]    = (fpu_state.tag >> (i * 2)) & 3;
            s->st[i]     = fpu_state.st_space[i].signif;
            s->st_exp[i] = fpu_state.st_space[i].signExp;
            s->mm[i]     = fpu_state.st_space[i].signif;
        }
    }
    s->ismmx     = cpu_state.ismmx;
    s->vector    = get32(HARNESS_RECORD + REC_VECTOR);
    s->tos       = get32(HARNESS_RECORD + REC_TOS);
    s->tsc_delta = get32(HARNESS_RECORD + REC_TSC_DELTA) | ((uint64_t) get32(HARNESS_RECORD + REC_TSC_DELTA + 4) << 32);
    memcpy(s->data, ram + HARNESS_DATA, HARNESS_PAGE);
    capture_stray_pages(s);
    capture_code_page(s);
}

/* The arm64 backend's blocks hold the guest's x87 and MMX registers in
   v8-v15 and return with them changed, though the low half of each is the
   caller's to keep (AAPCS64, 6.1.2). Named as clobbered in a function of its
   own, they are saved and restored by it, and the harness's callers find
   their own values there after a run. */
#if defined(__aarch64__)
static void __attribute__((noinline))
run_dynarec(int32_t cycs)
{
    exec386_dynarec(cycs);
    __asm__ volatile("" : : : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
}
#else
#    define run_dynarec(cycs) exec386_dynarec(cycs)
#endif

static int
run_until_halt(int dyn)
{
    int slices = 0;

    cpu_use_dynarec = dyn && !loop_2386;
    /* The read and write lookup tables charge cycles as they fill and would
       otherwise carry over from the previous run or case: every run starts
       with them empty and pays the same fills. */
    flushmmucache();
    for (;;) {
        cpu_halted   = 0;
        timer_target = tsc + 1000000; /* no timer is ever due */
        cycles       = 0;
        cycles_main  = 0;
        if (loop_2386)
            exec386_2386(SLICE_INT);
        else if (dyn)
            run_dynarec(SLICE_DYN);
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
        if (a->st_exp[i] != b->st_exp[i]) {
            snprintf(name, sizeof(name), "ST[%d].exp", i);
            diff(name, a->st_exp[i], b->st_exp[i]);
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
    if (a->code_bytes != b->code_bytes)
        diff("code.bytes", a->code_bytes, b->code_bytes);
    else if (a->code_hash != b->code_hash)
        diff("code.hash", a->code_hash, b->code_hash);
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
        if (fpu_softfloat) {
            printf("    ST[%d]=%04x:%016" PRIx64 "  MM%d=%016" PRIx64 "\n", i, s->st_exp[i], s->st[i], i, s->mm[i]);
            continue;
        }
        memcpy(&d, &s->st[i], 8);
        printf("    ST[%d]=%016" PRIx64 " (%.17g)  MM%d=%016" PRIx64 "\n", i, s->st[i], d, i, s->mm[i]);
    }
    if (s->stray_pages)
        printf("    stores outside the data page: %u page(s), first at %08x, hash %016" PRIx64 "\n", s->stray_pages, s->stray_first, s->stray_hash);
    if (s->code_bytes)
        printf("    stores to the code page: %u byte(s), first at %08x, hash %016" PRIx64 "\n", s->code_bytes, s->code_first, s->code_hash);
}

/* Same test as compare(), without the printing. */
static int
compare_silent(const harness_state_t *a, const harness_state_t *b)
{
    return a->status != b->status || memcmp(a->regs, b->regs, sizeof(a->regs)) || a->flags != b->flags
        || a->eflags != b->eflags || memcmp(a->seg, b->seg, sizeof(a->seg)) || memcmp(a->base, b->base, sizeof(a->base))
        || a->pc != b->pc || a->cr0_val != b->cr0_val || a->abrt != b->abrt || a->npxc != b->npxc || a->npxs != b->npxs
        || a->top != b->top || memcmp(a->tag, b->tag, sizeof(a->tag)) || memcmp(a->st, b->st, sizeof(a->st))
        || memcmp(a->st_exp, b->st_exp, sizeof(a->st_exp)) || memcmp(a->mm, b->mm, sizeof(a->mm)) || a->ismmx != b->ismmx || a->vector != b->vector || a->tos != b->tos
        || memcmp(a->data, b->data, sizeof(a->data)) || a->stray_pages != b->stray_pages || a->stray_hash != b->stray_hash
        || a->code_bytes != b->code_bytes || a->code_hash != b->code_hash;
}

/* ---- a case ---------------------------------------------------------- */

static int passes = MAX_RUNS;
static int dump   = 0;
static int quiet  = 0;
static int truncate_to = -1; /* cut a generated body to this many bytes */

/* The recompiler's blocks on one page as a signature: the block numbers,
   start addresses, lengths and whether each holds compiled code. A run that
   marked, compiled or dropped a block changes it. */
static uint64_t
page_blocks_sig(uint32_t phys, uint64_t sig)
{
    uint32_t nr = pages[phys >> 12].block;

    while (nr) {
        const codeblock_t *b = &codeblock[nr];

        sig = (sig ^ nr) * 0x100000001b3ULL;
        sig = (sig ^ b->pc) * 0x100000001b3ULL;
        sig = (sig ^ (uint32_t) b->ins) * 0x100000001b3ULL;
        sig = (sig ^ (b->flags & CODEBLOCK_WAS_RECOMPILED)) * 0x100000001b3ULL;
        nr  = b->next;
    }
    return sig;
}

static uint64_t
blocks_sig(void)
{
    uint64_t sig = page_blocks_sig(HARNESS_HANDLERS, page_blocks_sig(HARNESS_CODE, 0xcbf29ce484222325ULL));

    return page_blocks_sig(HARNESS_SMM_ENTRY, page_blocks_sig(HARNESS_SMM_ENTRY_CYRIX, sig));
}

/* The EIP the run's fault was taken at, from the frame its handler left on
   the stack (past the error code for the vectors that push one), or
   0xffffffff with no fault or no frame to read. Read before the next run
   reloads the stack page. */
static uint32_t
fault_eip(void)
{
    const uint32_t vector = get32(HARNESS_RECORD + REC_VECTOR);
    uint32_t       addr   = stack32 ? ESP : (ESP & 0xffff);

    if (vector == 0xffffffff)
        return 0xffffffff;
    if ((vector == 8) || ((vector >= 10) && (vector <= 14)) || (vector == 17))
        addr += 4;
    addr += cpu_state.seg_ss.base;
    if (addr > (mem_size * 1024) - 4)
        return 0xffffffff;
    return get32(addr);
}

/* Returns 0 when every run agrees, else a bit set: 1 state, 2 timing, 4 a
   generated body wrote its code page, 8 the harness's own code faulted or a
   generated body took #UD; and 16, not a failure, when the case ran none of
   its body (it was empty, faulted on its first instruction, or reset). */
static int
run_case(const harness_case_t *c)
{
    static harness_state_t st[MAX_RUNS + 1];
    static uint32_t        case_count;
    static int             audit_always = -1;
    int                    nruns;
    int                    compiled  = 0; /* the first run that changed no block */
    int                    converged = 0;
    int                    wrote_code = 0; /* 1 + the first run that changed the code page */
    int                    harness_fault = 0; /* 1 the prelude or epilogue faulted, 2 a generated body took #UD */
    int                    no_body   = 0;
    uint32_t               eip0;
    int                    result    = 0;

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
    /* The audit: the process's first case and every 256th, or every case
       with CPU_HARNESS_AUDIT=1 in the environment. */
    if (audit_always < 0) {
        const char *env = getenv("CPU_HARNESS_AUDIT");

        audit_always = env && (env[0] == '1');
    }
    load_case_audit = audit_always || !(case_count++ & 0xff);

    strcpy(run_name[0], "interpreter");
    step(run_name[0]);
    load_case(c, 1);
    step("run interpreter");
    st[0].status = run_until_halt(0);
    capture(&st[0], st[0].status);

    /* The prelude and the epilogue must run clean, and a generated body
       uses only what the CPU has: a fault in the harness's own code, or a
       #UD from the generator's, is a harness defect, and no run of it tests
       anything. An empty body, a fault at the body's first byte, or a reset,
       ran none of it. */
    eip0 = fault_eip();
    no_body = !c->body_len;
    if (st[0].status == RUN_RESET)
        no_body = 1;
    else if (eip0 != 0xffffffff) {
        const uint32_t code_end = HARNESS_CODE + code_len;

        if ((eip0 >= HARNESS_CODE) && (eip0 < body_start))
            harness_fault = 1;
        else if (((eip0 > body_end) || ((eip0 == body_end) && !c->body_len)) && (eip0 < code_end))
            harness_fault = 1;
        else if (c->generated && (st[0].vector == 6))
            harness_fault = 2;
        if ((eip0 >= HARNESS_CODE) && (eip0 <= body_start))
            no_body = 1;
    }

    /* Recompiler runs until one changes no block: a straight-line body
       compiles one more block per run, as each block is marked on the run
       after the one before it was compiled. That run ran compiled code
       only; one more runs it again, with the links the first one made. */
    for (nruns = 1; nruns <= passes; nruns++) {
        uint64_t before = blocks_sig();

        if (nruns == 1)
            strcpy(run_name[nruns], "mark pass");
        else if (!compiled)
            snprintf(run_name[nruns], sizeof(run_name[nruns]), nruns == 2 ? "compile pass" : "compile pass %d", nruns - 1);
        else
            strcpy(run_name[nruns], "compiled 2");
        step(run_name[nruns]);
        load_case(c, nruns == 1);
        step("run recompiler");
        st[nruns].status = run_until_halt(1);
        capture(&st[nruns], st[nruns].status);
        if (compiled) {
            nruns++;
            converged = 1;
            break;
        }
        if (nruns > 1 && blocks_sig() == before) {
            compiled = nruns;
            strcpy(run_name[nruns], "compiled");
        }
    }
    step("report");

    /* A hand body may write its code on purpose; a generated one must not. */
    for (int r = 0; r < nruns && c->generated && !wrote_code; r++) {
        if (st[r].code_bytes)
            wrote_code = r + 1;
    }

    if (quiet) {
        int bad = !converged || wrote_code || harness_fault;
        for (int r = 1; r < nruns && !bad; r++)
            bad = compare_silent(&st[0], &st[r]);
        if (!bad && st[compiled + 1].tsc_delta != st[compiled].tsc_delta)
            bad = 1;
        if (!bad) {
            cur_case = NULL;
            return no_body ? 16 : 0;
        }
    }

    printf("%s: ", c->label);
    for (int r = 1; r < nruns; r++) {
        if (compare(&st[0], &st[r])) {
            printf("\n  STATE %s != %s (%d fields)\n", run_name[0], run_name[r], diff_lines);
            result |= 1;
        }
    }
    if (!converged) {
        printf("\n  NOT CONVERGED within %d recompiler runs\n", nruns - 1);
        result |= 2;
    } else if (st[compiled + 1].tsc_delta != st[compiled].tsc_delta) {
        printf("\n  TIMING %s %" PRIu64 " != %s %" PRIu64 "\n", run_name[compiled], st[compiled].tsc_delta, run_name[compiled + 1], st[compiled + 1].tsc_delta);
        result |= 2;
    }
    if (wrote_code) {
        printf("\n  WRITES ITS CODE PAGE in %s: %u byte(s), first at %08x\n", run_name[wrote_code - 1],
               st[wrote_code - 1].code_bytes, st[wrote_code - 1].code_first);
        result |= 4;
    }
    if (harness_fault == 1) {
        printf("\n  FAULTS IN THE HARNESS CODE: vector %02x at %08x, body %08x-%08x\n", st[0].vector, eip0, body_start, body_end);
        result |= 8;
    } else if (harness_fault) {
        printf("\n  TAKES #UD at %08x: the generator drew an instruction this CPU lacks\n", eip0);
        result |= 8;
    }
    if (!result)
        printf("ok (%s", status_names[st[0].status]);
    else
        printf("  summary: (%s", status_names[st[0].status]);
    if (st[0].vector != 0xffffffff)
        printf(", fault %02x", st[0].vector);
    printf("; cycles interpreter %" PRIu64, st[0].tsc_delta);
    if (converged)
        printf(", %s %" PRIu64 ", compiled %" PRIu64, run_name[compiled - 1], st[compiled - 1].tsc_delta, st[compiled].tsc_delta);
    printf(")\n");

    if (result || dump) {
        for (int r = 0; r < nruns; r++) {
            printf("  %s:\n", run_name[r]);
            dump_state(&st[r]);
        }
        dump_case_hex(c);
    }
    cur_case = NULL;
    return result | (no_body ? 16 : 0);
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
            "  --fast-fpu-env       accurate FPU environment off: compiled x87 code leaves the\n"
            "                       FNSTENV/FNSAVE pointer words stale, so those cases differ\n"
            "  --nolink             block linking off\n"
            "  --loop2386           every run through exec386_2386(), the loop of a 286 or 386 with\n"
            "                       the recompiler off (a --cpu that runs it); nothing is compiled\n"
            "  --ram-kb <n>         RAM size in KB, 4096 (default) to 65536, a multiple of 4096\n"
            "  --cpu <family:name>  the CPU table entry, internal family name and CPU name\n"
            "                       (default pentium2_deschutes:600)\n"
            "  --fpu-type <n>       fpu_type forced to n (cpu.h's FPU_ order, default the CPU's own\n"
            "                       FPU, else the last coprocessor its table entry lists)\n"
            "  --dbg486             the 486 debug registers option on\n"
            "  --passes <n>         at most n recompiler runs after the interpreter run (default %d:\n"
            "                       mark pass, compile passes until one compiles nothing, compiled 2)\n"
            "  --dump               print every run's final state\n"
            "  --quiet              print only the cases with differences, and the total\n"
            "  --truncate <n>       cut each generated body to n bytes (bisecting a case)\n"
            "  --verbose            core log lines to stderr\n"
            "  --list <file>        hand cases in one process: every line of the file is\n"
            "                       \"<hex file> <stdout file> <stderr file>\" (no spaces in a name), and\n"
            "                       the two files get what --hex with the hex file's text and the\n"
            "                       other options given here prints, when the case has finished\n"
            "                       (until then it is <stdout file>.part, <stderr file>.part);\n"
            "                       not with --verbose\n",
            harness_gen_class_names(), MAX_RUNS);
}

/* The lines that end a run's output. */
static void
print_totals(int total, int failures, int code_writers, int harness_faults, int gen_no_body, int gen_total)
{
    printf("%d case%s, %d with differences, %d write their code page\n", total, total == 1 ? "" : "s", failures, code_writers);
    /* A sweep that ran nothing must not read as a pass. */
    if (harness_faults)
        printf("HARNESS DEFECT: %d case%s faulted in the prelude or epilogue or took #UD from the generator\n", harness_faults, harness_faults == 1 ? "" : "s");
    if (gen_no_body * 2 > gen_total)
        printf("VACUOUS: %d of %d generated cases, more than half, ran none of their body\n", gen_no_body, gen_total);
}

/* A whole file as a string, or NULL with errno set; a directory fails at
   the read. Zero bytes are left out, as the shell leaves them out of $(cat
   file). */
static char *
read_file(const char *name)
{
    FILE  *f   = fopen(name, "rb");
    size_t len = 0;
    size_t cap = 4096;
    char  *buf;
    int    ch;
    int    err = 0;

    if (!f)
        return NULL;
    buf = malloc(cap);
    while (buf && ((ch = getc(f)) != EOF)) {
        if (!ch)
            continue;
        if (len + 2 > cap) {
            char *grown = realloc(buf, cap * 2);

            if (!grown) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = grown;
            cap *= 2;
        }
        buf[len++] = (char) ch;
    }
    if (!buf)
        err = ENOMEM;
    else if (ferror(f))
        err = errno ? errno : EIO;
    fclose(f);
    if (err) {
        free(buf);
        errno = err;
        return NULL;
    }
    buf[len] = 0;
    return buf;
}

#ifdef _WIN32
#    define LIST_NULL_DEVICE "NUL"
#else
#    define LIST_NULL_DEVICE "/dev/null"
#endif

typedef struct list_entry_t {
    const char *hex_name;
    const char *out_name;
    const char *err_name;
    char       *out_part; /* <out_name>.part and <err_name>.part: the case prints into */
    char       *err_part; /* them, and they get their names when it has finished */
    char       *hex;      /* the hex file's text, as --hex takes it */
} list_entry_t;

/* The work files of the entry being run, and whether stdout and stderr are
   on them. */
static const char *list_out_part;
static const char *list_err_part;
static int         list_out_open;
static int         list_err_open;

#ifdef _WIN32
#    define LIST_CREATE_MODE (S_IREAD | S_IWRITE)
#else
#    define LIST_CREATE_MODE 0666
#endif

/* A work file made new: a name that is there already, after the old files
   were removed, is another entry's file under a spelling list_check() does
   not see through, and is left alone. Returns 0, or -1 with errno set. */
static int
list_create(const char *part)
{
    int fd = open(part, O_WRONLY | O_CREAT | O_EXCL, LIST_CREATE_MODE);

    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

static int
list_exists(const char *name)
{
    struct stat st;

    return !stat(name, &st);
}

/* stdout and stderr off the work files: Windows renames and removes no
   open file. A stream whose reopening failed is closed, and is left so. */
static void
list_park(void)
{
    FILE *f;

    if (list_out_open) {
        list_out_open = 0;
        f             = freopen(LIST_NULL_DEVICE, "w", stdout);
        (void) f;
    }
    if (list_err_open) {
        list_err_open = 0;
        f             = freopen(LIST_NULL_DEVICE, "w", stderr);
        (void) f;
    }
}

/* No work file outlives the process: also the exit handler, for the exits
   that do not come back to run_list() (a fatal() from the core). */
static void
list_abandon(void)
{
    list_park();
    if (list_out_part)
        remove(list_out_part);
    if (list_err_part)
        remove(list_err_part);
    list_out_part = NULL;
    list_err_part = NULL;
}

static void
list_free(char *text, list_entry_t *e, int n)
{
    for (int i = 0; i < n; i++) {
        free(e[i].out_part);
        free(e[i].err_part);
        free(e[i].hex);
    }
    free(e);
    free(text);
}

/* The list's text cut into its entries, the names left in place. Returns
   their number, or -1 for a line that is neither empty nor three names. */
static int
list_entries(char *text, list_entry_t **entries, const char *list)
{
    list_entry_t *e    = NULL;
    int           n    = 0;
    int           line = 0;

    while (*text) {
        char *end = strchr(text, '\n');
        char *name[4];
        int   names = 0;

        line++;
        if (end)
            *end++ = 0;
        else
            end = text + strlen(text);
        while (*text) {
            text += strspn(text, " \t\r");
            if (!*text)
                break;
            if (names < 4)
                name[names] = text;
            names++;
            text += strcspn(text, " \t\r");
            if (*text)
                *text++ = 0;
        }
        text = end;
        if (!names)
            continue;
        if (names != 3) {
            fprintf(stderr, "%s, line %d: a line is \"<hex file> <stdout file> <stderr file>\"\n", list, line);
            free(e);
            return -1;
        }
        if (!(n & (n + 1))) {
            list_entry_t *grown = realloc(e, (2 * (size_t) n + 1) * sizeof(*e));

            if (!grown) {
                perror(list);
                free(e);
                return -1;
            }
            e = grown;
        }
        memset(&e[n], 0, sizeof(e[n]));
        e[n].hex_name = name[0];
        e[n].out_name = name[1];
        e[n].err_name = name[2];
        n++;
    }
    *entries = e;
    return n;
}

/* The key that the names of one file share, whatever their spelling: the
   directory by what it is (its device and file number; on Windows its
   volume's serial number and its file index), which must be there, then
   the file's own name in one letter case (on Windows without the dots and
   spaces at its end, which name nothing there). Allocated, or NULL with
   errno set. */
static char *
list_key(const char *name)
{
    const char *base = name;
    size_t      dir_len;
    size_t      len;
    char       *dir;
    char       *key;
    char        id[64];

#ifdef _WIN32
    for (const char *p = name; *p; p++) {
        if ((*p == '/') || (*p == '\\'))
            base = p + 1;
    }
    /* A drive's letter alone is its current directory. */
    if ((base == name) && isalpha((unsigned char) name[0]) && (name[1] == ':'))
        base = name + 2;
#else
    if (strrchr(name, '/'))
        base = strrchr(name, '/') + 1;
#endif
    dir_len = (size_t) (base - name);
    dir     = malloc(dir_len + 2);
    if (!dir) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(dir, name, dir_len);
    dir[dir_len] = 0;
#ifdef _WIN32
    if (!dir_len || (dir[dir_len - 1] == ':'))
        strcat(dir, ".");
    {
        BY_HANDLE_FILE_INFORMATION info;
        HANDLE                     h    = INVALID_HANDLE_VALUE;
        int                        wn   = MultiByteToWideChar(CP_ACP, 0, dir, -1, NULL, 0);
        wchar_t                   *wdir = (wn > 0) ? malloc((size_t) wn * sizeof(*wdir)) : NULL;
        int                        ok   = 0;

        if (wdir && MultiByteToWideChar(CP_ACP, 0, dir, -1, wdir, wn))
            h = CreateFileW(wdir, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ok = GetFileInformationByHandle(h, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            CloseHandle(h);
        }
        free(wdir);
        free(dir);
        if (!ok) {
            errno = ENOENT;
            return NULL;
        }
        snprintf(id, sizeof(id), "%08lx:%08lx%08lx", (unsigned long) info.dwVolumeSerialNumber, (unsigned long) info.nFileIndexHigh, (unsigned long) info.nFileIndexLow);
    }
#else
    if (!dir_len)
        strcpy(dir, ".");
    {
        struct stat st;
        int         err = stat(dir, &st) ? errno : (S_ISDIR(st.st_mode) ? 0 : ENOTDIR);

        free(dir);
        if (err) {
            errno = err;
            return NULL;
        }
        snprintf(id, sizeof(id), "%llx:%llx", (unsigned long long) st.st_dev, (unsigned long long) st.st_ino);
    }
#endif
    len = strlen(base);
#ifdef _WIN32
    while (len && ((base[len - 1] == '.') || (base[len - 1] == ' ')))
        len--;
#endif
    key = malloc(strlen(id) + len + 2);
    if (!key) {
        errno = ENOMEM;
        return NULL;
    }
    sprintf(key, "%s/", id);
    for (size_t i = 0; i < len; i++)
        key[strlen(id) + 1 + i] = (char) tolower((unsigned char) base[i]);
    key[strlen(id) + 1 + len] = 0;
    return key;
}

typedef struct list_name_t {
    char       *canon; /* list_key() */
    const char *name;  /* as the list has it */
    int         input; /* the list or a hex file: two entries may share a hex file */
} list_name_t;

static int
list_name_cmp(const void *a, const void *b)
{
    return strcmp(((const list_name_t *) a)->canon, ((const list_name_t *) b)->canon);
}

/* What must hold before anything is removed or run, with the reason printed
   when it does not: every output file's directory is there; no output file
   and no work file is another one, the list or a hex file, by list_key();
   and one that is there is a plain file, and not the list or a hex file
   through a link (by device and file number, on a host whose stat() has
   file numbers). Returns 0, or -1. */
static int
list_check(const char *list, const list_entry_t *e, int n)
{
    const size_t nr    = 5 * (size_t) n + 1;
    list_name_t *names = calloc(nr, sizeof(*names));
    struct stat *in    = malloc(((size_t) n + 1) * sizeof(*in));
    char        *in_ok = malloc((size_t) n + 1);
    int          ret   = 0;

    if (!names || !in || !in_ok) {
        perror(list);
        ret = -1;
    }
    if (!ret) {
        names[0].name = list;
        for (int i = 0; i < n; i++) {
            list_name_t *m = &names[1 + 5 * (size_t) i];

            m[0].name = e[i].hex_name;
            m[1].name = e[i].out_name;
            m[2].name = e[i].err_name;
            m[3].name = e[i].out_part;
            m[4].name = e[i].err_part;
        }
        for (size_t i = 0; (i < nr) && !ret; i++) {
            names[i].input = !i || !((i - 1) % 5);
            names[i].canon = list_key(names[i].name);
            if (names[i].canon)
                continue;
            if (!names[i].input) {
                fprintf(stderr, "%s: %s: its directory: %s\n", list, names[i].name, strerror(errno));
                ret = -1;
            } else {
                /* An input that cannot be reached fails where it is read. */
                names[i].canon = malloc(strlen(names[i].name) + 1);
                if (names[i].canon)
                    strcpy(names[i].canon, names[i].name);
                else {
                    perror(list);
                    ret = -1;
                }
            }
        }
    }
    if (!ret) {
        qsort(names, nr, sizeof(*names), list_name_cmp);
        for (size_t i = 1; (i < nr) && !ret; i++) {
            if (!strcmp(names[i - 1].canon, names[i].canon) && !(names[i - 1].input && names[i].input)) {
                fprintf(stderr, "%s: %s and %s are one file: an output file, and its work file <name>.part, can be no other file of the list\n", list, names[i - 1].name, names[i].name);
                ret = -1;
            }
        }
    }
    if (!ret) {
        in_ok[0] = !stat(list, &in[0]);
        for (int i = 0; i < n; i++)
            in_ok[1 + i] = !stat(e[i].hex_name, &in[1 + i]);
    }
    for (int i = 0; (i < n) && !ret; i++) {
        const char *out[4] = { e[i].out_name, e[i].err_name, e[i].out_part, e[i].err_part };

        for (int k = 0; (k < 4) && !ret; k++) {
            struct stat st;

            if (stat(out[k], &st))
                continue;
            if (!S_ISREG(st.st_mode)) {
                fprintf(stderr, "%s: %s is there and is not a file\n", list, out[k]);
                ret = -1;
            }
            for (int j = 0; (j <= n) && !ret && st.st_ino; j++) {
                if (in_ok[j] && (in[j].st_ino == st.st_ino) && (in[j].st_dev == st.st_dev)) {
                    fprintf(stderr, "%s: %s is %s\n", list, out[k], j ? e[j - 1].hex_name : "the list");
                    ret = -1;
                }
            }
        }
    }
    for (size_t i = 0; names && (i < nr); i++)
        free(names[i].canon);
    free(names);
    free(in);
    free(in_ok);
    return ret;
}

/* --list: each entry's case runs as the only case of a process of its own
   would. Before any case runs the whole list is checked (list_check()), the
   entries' old output and work files are removed and every hex file is
   read: a failure there is exit code 2 with nothing run. A case prints into
   <stdout file>.part and <stderr file>.part, which get their names when it
   has finished, so an output file that is there is a finished case's
   whatever ends the process, and no work file is left. Returns the exit
   code: the greatest a process per case would give (0 every case agrees, 1
   one does not, 2 one has a bad hex body); 2 as above; 3 for an output file
   that cannot be written. */
static int
run_list(const char *list, harness_case_t *c, int mode16)
{
    static int    handler;
    char         *text = read_file(list);
    list_entry_t *e    = NULL;
    int           n    = 0;
    int           fd;
    int           rc  = 2;
    int           ran = 0;

    if (!text) {
        perror(list);
        return 2;
    }
    n = list_entries(text, &e, list);
    if (n <= 0) {
        if (!n)
            fprintf(stderr, "%s: no entry\n", list);
        free(text);
        return 2;
    }
    for (int i = 0; i < n; i++) {
        e[i].out_part = malloc(strlen(e[i].out_name) + 6);
        e[i].err_part = malloc(strlen(e[i].err_name) + 6);
        if (!e[i].out_part || !e[i].err_part) {
            perror(list);
            goto done;
        }
        sprintf(e[i].out_part, "%s.part", e[i].out_name);
        sprintf(e[i].err_part, "%s.part", e[i].err_name);
    }
    if (list_check(list, e, n))
        goto done;
    for (int i = 0; i < n; i++) {
        const char *old[4] = { e[i].out_name, e[i].err_name, e[i].out_part, e[i].err_part };

        for (int k = 0; k < 4; k++) {
            if (remove(old[k]) && (errno != ENOENT)) {
                fprintf(stderr, "%s: %s cannot be removed: %s\n", list, old[k], strerror(errno));
                goto done;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        size_t len;

        e[i].hex = read_file(e[i].hex_name);
        if (!e[i].hex) {
            perror(e[i].hex_name);
            goto done;
        }
        /* Without the line ends at its end, as $(cat file) gives it. */
        len = strlen(e[i].hex);
        while (len && (e[i].hex[len - 1] == '\n'))
            e[i].hex[--len] = 0;
    }
    rc = 3;
    fflush(stderr);
    fd       = dup(fileno(stderr));
    list_log = (fd < 0) ? NULL : fdopen(fd, "w");
    if (!list_log) {
        perror(list);
        if (fd >= 0)
            close(fd);
        goto done;
    }
    setvbuf(list_log, NULL, _IONBF, 0);
    if (!handler) {
        handler = 1;
        atexit(list_abandon);
    }

    rc = 0;
    for (int i = 0; i < n; i++) {
        int written;

        list_entry    = i + 1;
        list_hex_name = e[i].hex_name;
        fflush(stdout);
        fflush(stderr);
        /* A work file is the entry's to remove once it has made it. */
        if (list_create(e[i].out_part)) {
            fprintf(list_log, "list entry %d (%s): %s: %s; it and the entries after it have no output files\n", list_entry, list_hex_name, e[i].out_part, strerror(errno));
            rc = 3;
            break;
        }
        list_out_part = e[i].out_part;
        list_out_open = 1;
        if (!freopen(e[i].out_part, "w", stdout)) {
            list_out_open = 0;
            fprintf(list_log, "list entry %d (%s): %s: %s; it and the entries after it have no output files\n", list_entry, list_hex_name, e[i].out_part, strerror(errno));
            rc = 3;
            break;
        }
        if (list_create(e[i].err_part)) {
            fprintf(list_log, "list entry %d (%s): %s: %s; it and the entries after it have no output files\n", list_entry, list_hex_name, e[i].err_part, strerror(errno));
            rc = 3;
            break;
        }
        list_err_part = e[i].err_part;
        list_err_open = 1;
        if (!freopen(e[i].err_part, "w", stderr)) {
            list_err_open = 0;
            fprintf(list_log, "list entry %d (%s): %s: %s; it and the entries after it have no output files\n", list_entry, list_hex_name, e[i].err_part, strerror(errno));
            rc = 3;
            break;
        }
        default_case(c);
        c->body_len = parse_hex(e[i].hex, c->body, HARNESS_BODY_MAX);
        if (c->body_len < 0) {
            fprintf(stderr, "bad hex body\n");
            rc = 2;
        } else {
            int r;

            c->mode16 = mode16;
            if (ran++)
                reset_machine();
            r = run_case(c);
            print_totals(1, !!(r & 3), !!(r & 4), !!(r & 8), 0, 0);
            if ((r & 15) && !rc)
                rc = 1;
        }
        written = !fflush(stdout);
        written &= !fflush(stderr);
        written &= !ferror(stdout) && !ferror(stderr);
        list_park();
        /* Neither name is there, the old files removed: one that is, is
           another entry's file under another spelling, and stays. */
        if (!written || list_exists(e[i].out_name) || list_exists(e[i].err_name) || rename(e[i].out_part, e[i].out_name)) {
            fprintf(list_log, "list entry %d (%s): %s, %s cannot be written; it and the entries after it have no output files\n", list_entry, list_hex_name, e[i].out_name, e[i].err_name);
            rc = 3;
            break;
        }
        if (rename(e[i].err_part, e[i].err_name)) {
            remove(e[i].out_name);
            fprintf(list_log, "list entry %d (%s): %s cannot be written; it and the entries after it have no output files\n", list_entry, list_hex_name, e[i].err_name);
            rc = 3;
            break;
        }
        list_out_part = NULL;
        list_err_part = NULL;
    }
    list_abandon();
    list_hex_name = NULL;
    fclose(list_log);
    list_log = NULL;
done:
    list_free(text, e, n);
    return rc;
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
    int             fast_fpu_env = 0;
    int             nolink     = 0;
    uint32_t        classes    = GEN_ALL;
    int             failures   = 0;
    int             code_writers = 0;
    int             harness_faults = 0;
    int             gen_total  = 0;
    int             gen_no_body = 0;
    int             total      = 0;
    int             r;
    const char     *list       = NULL;

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
        } else if (!strcmp(argv[i], "--fast-fpu-env")) {
            fast_fpu_env = 1;
        } else if (!strcmp(argv[i], "--nolink")) {
            nolink = 1;
        } else if (!strcmp(argv[i], "--loop2386")) {
            loop_2386 = 1;
        } else if (!strcmp(argv[i], "--ram-kb") && i + 1 < argc) {
            char *end;
            long  v = strtol(argv[++i], &end, 10);

            if (*end || (v < HARNESS_RAM_KB) || (v > 65536) || (v % 4096)) {
                fprintf(stderr, "--ram-kb takes 4096 to 65536, a multiple of 4096\n");
                return 2;
            }
            machine_ram_kb = (int) v;
        } else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) {
            int end = 0;

            /* The whole argument, or nothing: a name too long for the buffer
               is refused, not cut short */
            i++;
            if ((sscanf(argv[i], "%63[^:]:%31[^\n]%n", machine_family, machine_cpu_name, &end) != 2) || argv[i][end]) {
                fprintf(stderr, "--cpu takes family:name\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--fpu-type") && i + 1 < argc) {
            char *end;
            long  v = strtol(argv[++i], &end, 10);

            if ((end == argv[i]) || *end || (v < FPU_NONE) || (v > FPU_INTERNAL)) {
                fprintf(stderr, "fpu-type must be %d..%d\n", FPU_NONE, FPU_INTERNAL);
                return 2;
            }
            machine_fpu_type = (int) v;
        } else if (!strcmp(argv[i], "--dbg486")) {
            machine_dbg486 = 1;
        } else if (!strcmp(argv[i], "--passes") && i + 1 < argc) {
            passes = atoi(argv[++i]);
            if (passes < 1 || passes > MAX_RUNS) {
                fprintf(stderr, "passes must be 1..%d\n", MAX_RUNS);
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
        } else if (!strcmp(argv[i], "--list") && i + 1 < argc) {
            list = argv[++i];
        } else {
            usage();
            return 2;
        }
    }
    /* A list is the whole run: its cases are hand bodies of their own. */
    if ((!have_body && !have_seeds && !list) || (list && (have_body || have_seeds))) {
        usage();
        return 2;
    }
    /* The core's log lines of a list case would be the reset's, where a
       process of its own prints its setup's. */
    if (list && harness_verbose) {
        fprintf(stderr, "--list does not take --verbose\n");
        return 2;
    }

    setup_machine(softfloat, fast_fpu_env);
    if (nolink)
        cpu_dyn_block_link = 0;

    /* All the classes means all this CPU has; a class it lacks, asked for
       by name, is refused. */
    {
        uint32_t lacks = (harness_model.x87 ? 0 : GEN_X87) | (harness_model.mmx ? 0 : GEN_MMX);

        if (have_seeds && (classes & lacks)) {
            if (classes != GEN_ALL) {
                fprintf(stderr, "--class asks for %s, which this CPU and FPU lack\n", (classes & lacks & GEN_X87) ? "x87" : "mmx");
                return 2;
            }
            classes &= ~lacks;
        }
    }

    if (list) {
        r = run_list(list, c, mode16);
        free(c);
        return r;
    }
    if (have_body) {
        c->mode16 = mode16;
        total++;
        r = run_case(c);
        failures += !!(r & 3);
        code_writers += !!(r & 4);
        harness_faults += !!(r & 8);
    }
    if (have_seeds) {
        for (uint64_t s = seed_a; s <= seed_b; s++) {
            harness_gen_case(c, s, classes);
            c->mode16 = mode16;
            if (truncate_to >= 0 && truncate_to < c->body_len)
                c->body_len = truncate_to;
            total++;
            r = run_case(c);
            failures += !!(r & 3);
            code_writers += !!(r & 4);
            harness_faults += !!(r & 8);
            gen_total++;
            gen_no_body += !!(r & 16);
        }
    }

    print_totals(total, failures, code_writers, harness_faults, gen_no_body, gen_total);
    free(c);
    return (failures || code_writers || harness_faults || (gen_no_body * 2 > gen_total)) ? 1 : 0;
}
