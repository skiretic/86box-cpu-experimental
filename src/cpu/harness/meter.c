/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the timing-model meter (--meter <book.tsv>).
 *
 *          A book file is tab-separated with a header line naming its
 *          columns: parts, bytes, kind, cond, meaning, book, doc, page,
 *          status, note, and optionally row_id, code_row and ruling. A row
 *          is measured on the CPU set up when its parts name it (the
 *          family's internal name, family:name, the CPU's name, a
 *          --meter-parts alias, or "*"), or with --meter-all. A part with no
 *          book of its own (--meter-rider) is measured on another part's
 *          rows and never scored: its verdicts read RIDER. A part whose
 *          timing model no book speaks for (--meter-nobook) is measured on
 *          every row, every verdict NO-BOOK, so a change to its model shows.
 *
 *          bytes is one instance, lowercase hex separated by spaces, in a
 *          32-bit code segment; several instances are separated by ';'. An
 *          instance is one instruction, or FWAITs and the x87 instruction
 *          they are written ahead of ("9b db e2"): each FWAIT is an
 *          instruction of its own, as the CPU loop fetches it, and the row
 *          reads the x87 instruction's table entry. kind is single (one
 *          instance alone in a block), chain or stream (the instances
 *          repeated), pair:<hex> (bytes then hex, as one unit),
 *          seq:<hex;hex;...> (the listed unit), rep:<n1>,<n2> or rep:c|n
 *          (the instance with ECX = n1 and n2, 4 and 12 for c or n: per
 *          element from the end-to-end runs; a book "a+b<count>" is scored
 *          on b, "a+<count>" on 1), or struct. meaning uops, decoder,
 *          pairing and ports, and kind struct, compare the book's values
 *          with what the model's table holds (meter_struct_*.c); every other
 *          meaning compares clocks per instruction (chain, stream) or per
 *          unit (single, pair, seq). A row of prefix bytes alone is timed on
 *          a carrier instruction: the prefixed carrier's totals minus the
 *          carrier's, then per copy.
 *
 *          A latency (a single row whose meaning is latency, or kind chain)
 *          is timed on copies that each read a value the one before made
 *          (unit_chains(): the general, MMX and x87 registers and the flags
 *          each instruction reads and writes by its own definition,
 *          insn_rw(), never a model's table); where the row's bytes do not,
 *          on the chain the same instruction allows (rebuild_chain()), else
 *          the row is NOCHAIN and not scored. A throughput (meaning
 *          throughput, or kind stream) is timed on copies that read none of
 *          each other's values; where they do, the row is NOSTREAM and not
 *          scored. A Pentium book's figure for an x87 instruction or FWAIT
 *          (the Pentium model) is a latency; its "a/b" figures are latency a
 *          and throughput b (Pentium Vol. 3 F.3.2), two rows (row_id /lat,
 *          /tput). Where its copies read none of each other's values, a
 *          latency the book gives equal to the throughput (one figure, or a
 *          = b) is timed on them as they are (fp_same()); a /tput row whose
 *          copies read each other is timed on the stream x87_stream() builds
 *          (other ST(i) in turn, or FXCH between copies where the book pairs
 *          the instruction with it). A latency whose book figures differ (a
 *          != b) of an instruction whose copies read none of each other's
 *          values (a load or constant push, a compare whose result is the
 *          condition codes, FNSTSW, FNINIT, FNSTENV) shows only as another
 *          instruction's wait, whose own figures would be in the measure:
 *          NOCHAIN, delta "unscored:needs-consumer".
 *
 *          model: the model's hooks driven as codegen_generate_call()
 *          drives them, from the model's cleared state, over one block of
 *          LO and then HI copies of the unit; the charge is the difference
 *          (a single: one instance alone, minus an empty block). Blocks
 *          split as the recompiler splits them (drive()): an always-taken
 *          branch (JMP, CALL, RET imm, far RET, IRET, FF /2-/5;
 *          branch_kind()) and an op whose compile ends its block
 *          (op_ends_block(): near RET, HLT, MOV CR0, CR3 or CR4, INVLPG,
 *          LSS, FNINIT, an MMX op entering MMX mode, ...) end it with
 *          block_end()'s drain, STI and a compiled MOV SS after the
 *          instruction that follows them; a conditional branch the row takes
 *          (cond taken) leaves with the cycles so far plus the taken path's
 *          held cycle (the model's jump_cycles(), or timing_bt for a Jcc on
 *          a model without that hook); INT3, INT n, INT1, POP SS and a MOV
 *          SS left to its op leave after their call with the cycles so far
 *          and the held cycle (POP SS's and MOV SS's op runs the next
 *          instruction itself, on the interpreter's charge). The copies go
 *          on in the next block. LO is the first copy count past which the
 *          model's total and the frame's (ctx) both grow the same every
 *          period, and HI - LO whole periods (choose_window()): a pipeline
 *          that starts empty (the Pentium FPU's) charges less for the first
 *          copies, and copies next to the frame pack with it.
 *
 *          ctx: the same hooks over the exact byte stream the end-to-end
 *          runs time: the harness's prelude, the warm-up, the copies, and
 *          the epilogue up to its time read, blocks ending where the
 *          recompiler ends them, as for model. wire_c / wire_i: that stream
 *          as a hand case, compiled (the "compiled" run, which must equal
 *          "compiled 2") and interpreted (run 0); the TSC deltas,
 *          differenced the same way. A single row's ctx and wire time its
 *          instance alone instead of copies: the last instruction of its
 *          block, separators ahead of and after it, the run with it minus
 *          the run without it (single_stream()); also in a second frame
 *          of other separators, the two counted (COUNT FRAME-SAME,
 *          FRAME-DIFF). On an in-order model a ctx other than the isolated
 *          block's figure (model) is the frame's: NOT-MEASURED, delta
 *          alone-not-isolated-block(...), as are an instance that does not
 *          end its block, one whose opcode lies past its bytes (a lone 0F)
 *          and a single row of an out-of-order model (P6, K6), which no
 *          separator isolates, where a run was made. wire is the flag of
 *          the model against what is charged: "=" when wire_c equals ctx
 *          plus the charges outside the model (off_model()) and the
 *          model's own figure plus them does too; "=ctx" when wire_c equals ctx's but the model's
 *          does not (COUNT WIRE-MODEL-CTX); GAP when wire_c does not equal
 *          ctx's: the compiled path charging what the meter does not follow.
 *          A
 *          relative branch runs with its displacement set to 0 (its target
 *          the next instruction; no model reads the displacement), with the
 *          flags and ECX that put it on the row's path. A REPE or REPNE
 *          CMPS or SCAS runs on data that keeps it going for every element.
 *          A body that faults, halts early, or holds a far, indirect or
 *          return branch or an interrupt (it would leave the copies, and
 *          could land past the epilogue's time read and still reach its
 *          HLT) is not timed: norun.
 *
 *          verdict scores against the book what the emulator charges: wire_c
 *          where the compiled run ran on the row's path; else ctx plus the
 *          charges outside the model, reproduced from the variables the
 *          compiled code charges (off_model(): far transfers, INT,
 *          misaligned accesses; a cond misaligned row its penalty alone);
 *          else NOT-MEASURED with its reason in the delta, as are the rows
 *          whose outside charges the meter does not reproduce, among them
 *          the charges an op writes as a literal or an expression of its
 *          own (HLT, CMPXCHG8B, RCL/RCR by a count, which misaligned
 *          accesses cpu_cyrix_alignment leaves charged), which no variable
 *          holds for the meter to read. The delta names the source (measured=wire_c,
 *          reproduced=...). verdict_i scores wire_i (the interpreter's own
 *          charges, x87_timings and the REP element cycles among them); a
 *          rep row counts as C-<verdict>. A
 *          row whose documents give different figures (status split:...)
 *          is SPLIT, its delta the status with every document's figure.
 *          code_row names the model's table entry, code_row_i the
 *          interpreter-side charge the row reaches, code_row_d the
 *          opcode_deps entry (codegen_timing_common.c, every model's) the
 *          model reads beside it, for the home-side SHARED report. A row
 *          with a conditional branch is timed on the row's path (cond taken
 *          or nottaken; not taken where it names none) and also on the
 *          other (ctx_alt): a run that matches the other path reads wire
 *          "otherpath", and its verdict_i is BRANCH, unscored, as is one
 *          that matches neither. Every figure is an integer total; per-copy
 *          figures are printed with two decimals from integers, so the
 *          output is the same on every host.
 */
#include <ctype.h>
#include <inttypes.h>
#include <setjmp.h>
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
#include "x87_sf.h"
#include "x87.h"
#include <86box/mem.h>
#include "386_common.h"
#include "codegen.h"
#include "codegen_timing_common.h"

#include "harness.h"
#include "meter.h"

enum {
    C_PARTS,
    C_BYTES,
    C_KIND,
    C_COND,
    C_MEANING,
    C_BOOK,
    C_DOC,
    C_PAGE,
    C_STATUS,
    C_NOTE,
    C_ROWID,
    C_CODEROW,
    C_RULING,
    NCOL
};

static const char *col_names[NCOL] = {
    "parts", "bytes", "kind", "cond", "meaning", "book", "doc", "page", "status", "note", "row_id", "code_row", "ruling"
};

#define INS_MAX    15
#define UNIT_MAX   8
#define STREAM_MAX 96
#define BUF_MAX    3072
/* Body instructions that share the first block with the prelude (7), the
   warm-up (5) and the epilogue (12) under the recompiler's limit of 50. */
#define BLOCK_FIT 24

typedef struct ins_t {
    uint8_t b[INS_MAX];
    int     len;
} ins_t;

/* A Pentium book's x87 row (pentium_fp()): its one figure, or the /lat or
   /tput half of an "a/b" one. */
enum {
    FP_NONE,
    FP_ONE,
    FP_LAT,
    FP_TPUT
};

typedef struct row_t {
    char       *f[NCOL];
    char       *id;  /* row_id, or one made from bytes, kind, cond and meaning */
    char       *own; /* the book figures of a split row (pentium_fp()), or NULL */
    int         line;
    int         fp;  /* FP_* */
    const char *fp_a; /* FP_LAT, FP_TPUT: the book's latency a and throughput b */
    const char *fp_b;
    int         fx;  /* the book pairs the instruction with FXCH (its pairing row reads FX) */
} row_t;

/* ---- fixed-point figures --------------------------------------------- */

static void
fmt_x100(char *out, size_t n, int64_t v)
{
    const char *sign = (v < 0) ? "-" : "";
    uint64_t    a    = (v < 0) ? (uint64_t) -v : (uint64_t) v;

    snprintf(out, n, "%s%" PRIu64 ".%02" PRIu64, sign, a / 100, a % 100);
}

/* total / count, times 100, rounded half away from zero. */
static int64_t
per_x100(int64_t total, int count)
{
    uint64_t a = (total < 0) ? (uint64_t) -total : (uint64_t) total;
    uint64_t q = (a * 200 + (uint64_t) count) / (2 * (uint64_t) count);

    return (total < 0) ? -(int64_t) q : (int64_t) q;
}

/* A book figure: digits with up to two decimals. Returns 0 if s is not one. */
static int
parse_x100(const char *s, const char *end, int64_t *v)
{
    int64_t whole = 0;
    int     frac  = 0;
    int     fd    = 0;
    int     any   = 0;

    while ((s < end) && isspace((unsigned char) *s))
        s++;
    while ((end > s) && isspace((unsigned char) end[-1]))
        end--;
    for (; (s < end) && isdigit((unsigned char) *s); s++, any = 1)
        whole = whole * 10 + (*s - '0');
    if ((s < end) && (*s == '.')) {
        for (s++; (s < end) && isdigit((unsigned char) *s); s++, any = 1) {
            if (fd < 2) {
                frac = frac * 10 + (*s - '0');
                fd++;
            } else if (*s != '0')
                return 0;
        }
    }
    if (!any || (s != end))
        return 0;
    while (fd++ < 2)
        frac *= 10;
    *v = whole * 100 + frac;
    return 1;
}

/* ---- instances ------------------------------------------------------- */

/* Hex instances separated by ';'. Returns the count, or -1. */
static int
parse_units(const char *s, ins_t *out, int max)
{
    int n = 0;

    while (*s) {
        ins_t *in = &out[n];

        if (n >= max)
            return -1;
        in->len = 0;
        while (*s && (*s != ';')) {
            unsigned v;
            int      used;

            if (isspace((unsigned char) *s)) {
                s++;
                continue;
            }
            if ((sscanf(s, "%2x%n", &v, &used) != 1) || (used != 2) || (in->len >= INS_MAX))
                return -1;
            in->b[in->len++] = v;
            s += 2;
        }
        if (!in->len)
            return -1;
        n++;
        if (*s == ';')
            s++;
    }
    return n;
}

static int
is_prefix_byte(uint8_t b)
{
    return (b == 0x26) || (b == 0x2e) || (b == 0x36) || (b == 0x3e) || (b == 0x64) || (b == 0x65) || (b == 0x66) ||
           (b == 0x67) || (b == 0xf0) || (b == 0xf2) || (b == 0xf3);
}

/* The harness's frame as instances. Its prelude and epilogue use only
   these forms: mov [disp32],eax / mov eax,[disp32] (a3, a1), the ModR/M
   disp32 forms of mov, sub and sbb (89, 8b, 2b, 1b), rdtsc, pushfd, popfd,
   out imm8,al and hlt. ntime is the number of epilogue instances up to and
   including the one that reads the time. */
static ins_t frame_pre[16];
static ins_t frame_epi[16];
static int   nframe_pre;
static int   nframe_epi;
static int   nframe_time;

static int
frame_split(const uint8_t *p, int len, ins_t *out, int *time_at)
{
    int n = 0;

    *time_at = -1;
    for (int i = 0; i < len;) {
        int l;

        switch (p[i]) {
            case 0xa1:
            case 0xa3:
                l = 5;
                break;
            case 0x89:
            case 0x8b:
            case 0x2b:
            case 0x1b:
                l = 6;
                break;
            case 0x0f:
                l = 2;
                if (p[i + 1] == 0x31)
                    *time_at = n;
                break;
            case 0xe6:
                l = 2;
                if (p[i + 1] == HARNESS_PORT_TSC_END)
                    *time_at = n;
                break;
            case 0x9c:
            case 0x9d:
            case 0xf4:
                l = 1;
                break;
            default:
                return -1;
        }
        if ((n >= 16) || (i + l > len))
            return -1;
        memcpy(out[n].b, p + i, l);
        out[n].len = l;
        n++;
        i += l;
    }
    return n;
}

static int
frame_setup(void)
{
    uint8_t pre[128];
    uint8_t epi[128];
    int     npre;
    int     nepi;
    int     t;

    harness_meter_frame(pre, &npre, epi, &nepi);
    nframe_pre = frame_split(pre, npre, frame_pre, &t);
    nframe_epi = frame_split(epi, nepi, frame_epi, &nframe_time);
    if ((nframe_pre < 0) || (nframe_epi < 0) || (nframe_time < 0))
        return -1;
    nframe_time++;
    return 0;
}

/* Every run starts with the lookup tables empty, and the first access to a
   page charges its fill: a warm-up ahead of the copies reads and writes the
   data page (EAX, EBX, ESI and EDI point into it) and the stack page (ESP,
   EBP), and leaves every register as it found it, so the copies'
   difference holds no fill. xchg [ebx],eax twice; push eax; pop eax; then
   CLD (DF is clear already). */
static const ins_t warm[] = {
    { { 0x87, 0x03 }, 2 },
    { { 0x87, 0x03 }, 2 },
    { { 0x50 }, 1 },
    { { 0x58 }, 1 },
    { { 0xfc }, 1 }
};
#define NWARM ((int) (sizeof(warm) / sizeof(warm[0])))

/* ---- driving the model ----------------------------------------------- */

static jmp_buf meter_jmp;
static char    meter_fatal_msg[128];

static void __attribute__((noreturn))
meter_fatal(const char *msg)
{
    snprintf(meter_fatal_msg, sizeof(meter_fatal_msg), "%s", msg);
    for (char *p = meter_fatal_msg; *p; p++) {
        if ((*p == '\n') || (*p == '\t'))
            *p = ' ';
    }
    longjmp(meter_jmp, 1);
}

/* The instructions sit in guest RAM at the code page, where the K6 model
   and its table reader (meter_struct_k6.c) read a 3DNow! suffix; the code
   segment is flat while the model runs. */
static uint8_t  saved_code[BUF_MAX];
static uint32_t saved_cs;

static void
place(const uint8_t *buf, int len)
{
    memcpy(saved_code, &ram[HARNESS_CODE], BUF_MAX);
    memset(&ram[HARNESS_CODE], 0x90, BUF_MAX);
    memcpy(&ram[HARNESS_CODE], buf, len);
    saved_cs            = cs;
    cs                  = 0;
    pccache             = 0xffffffff;
    codegen_timing_peek = 0;
}

static void
unplace(void)
{
    memcpy(&ram[HARNESS_CODE], saved_code, BUF_MAX);
    cs                  = saved_cs;
    pccache             = 0xffffffff;
    codegen_timing_peek = 0;
}

static uint32_t
rd32(uint32_t a)
{
    return ram[a] | (ram[a + 1] << 8) | (ram[a + 2] << 16) | ((uint32_t) ram[a + 3] << 24);
}

/* x87 TOP and the valid tags as a row's instructions move them
   (x87_effect()); bad: an instruction read an empty register or pushed
   into a valid one. */
typedef struct x87_sim_t {
    int     top;
    uint8_t valid[8];
    int     bad;
} x87_sim_t;

static int x87_effect(x87_sim_t *s, int op, int modrm);

typedef struct walk_t {
    uint8_t  last_prefix; /* 0, 0x0f or the escape, as the opcode hook's tables see it */
    uint8_t  opcode;
    uint32_t fetchdat;
    int      op_32;
    uint32_t op_pc;
    int      nprefix;
} walk_t;

/* One instruction at a, as codegen_generate_call() walks it: start(), a
   prefix() for every prefix, 0F and escape byte with the bytes the
   interpreter's fetches hold there, then opcode(). With hooks 0 only the
   walk. */
static void
walk(uint32_t a, int hooks, walk_t *w)
{
    uint32_t pc       = a + 1;
    uint8_t  opcode   = ram[a];
    uint32_t fetchdat = rd32(a) >> 8;
    int      op_32    = 0x300;
    int      over     = 0;
    int      pc_off   = 0;
    int      in_lock  = 0;
    uint32_t lock_pc  = 0;
    int      illegal  = 0;
    uint8_t  last     = 0;
    int      nprefix  = 0;

    if (hooks)
        codegen_timing_start();
    while (!over) {
        switch (opcode) {
            case 0x0f:
                over = 1;
                break;
            case 0x26:
            case 0x2e:
            case 0x36:
            case 0x3e:
            case 0x64:
            case 0x65:
            case 0xf2:
            case 0xf3:
                break;
            case 0x66:
                op_32 = 0x000 | (op_32 & 0x200);
                break;
            case 0x67:
                op_32 = 0x000 | (op_32 & 0x100);
                break;
            case 0xd8:
            case 0xd9:
            case 0xda:
            case 0xdb:
            case 0xdc:
            case 0xdd:
            case 0xde:
            case 0xdf:
                over   = 1;
                pc_off = -1;
                break;
            case 0xf0:
                if (!in_lock)
                    lock_pc = pc;
                in_lock = 1;
                break;
            default:
                goto done;
        }
        if ((opcode != 0x0f) && ((opcode & 0xf8) != 0xd8))
            fetchdat = rd32(pc);
        if (hooks)
            codegen_timing_prefix(opcode, fetchdat);
        nprefix++;
        last   = opcode;
        opcode = fetchdat & 0xff;
        if (!pc_off)
            fetchdat >>= 8;
        pc++;
    }
done:
    if (in_lock) {
        const int is_0f = (last == 0x0f);

        if (((over ? (pc - 2) : (pc - 1)) - lock_pc) > 14)
            illegal = 1;
        else if ((last & 0xf8) == 0xd8)
            illegal = 1;
        else
            illegal = !x86_lock_legal(is_0f, opcode, fetchdat & 0xff);
    }
    if (hooks) {
        codegen_timing_peek = illegal;
        codegen_timing_opcode(opcode, fetchdat, op_32, pc);
        codegen_timing_peek = 0;
    }
    w->last_prefix = ((last == 0x0f) || ((last & 0xf8) == 0xd8)) ? last : 0;
    w->opcode      = opcode;
    w->fetchdat    = fetchdat;
    w->op_32       = op_32;
    w->op_pc       = pc;
    w->nprefix     = nprefix;
}

/* Where the instruction walk() found ends, in an instance that ends at
   end. An instance holds one instruction, but for an FWAIT written ahead
   of an x87 one ("9b db e2", FCLEX): the CPU loop fetches the FWAIT as an
   instruction of its own, and codegen_generate_call() is called for it and
   then for the escape. */
static uint32_t
insn_end(const walk_t *w, uint32_t end)
{
    if (!w->last_prefix && !w->nprefix && (w->opcode == 0x9b))
        return w->op_pc;
    return end;
}

/* The instance past the FWAITs written ahead of it, the instruction whose
   table entry the row reads; the instance itself if nothing follows them. */
static void
past_fwait(const ins_t *in, ins_t *out)
{
    int i = 0;

    while ((i < in->len - 1) && (in->b[i] == 0x9b))
        i++;
    out->len = in->len - i;
    memcpy(out->b, in->b + i, out->len);
}

/* 0 when an instance's last instruction has its opcode past the
   instance's bytes (a lone 0F): it would take the next instance's. */
static int
whole_insns(const ins_t *unit, int nunit)
{
    walk_t w;
    int    ok = 1;

    for (int i = 0; ok && (i < nunit); i++) {
        place(unit[i].b, unit[i].len);
        for (uint32_t a = HARNESS_CODE, end = HARNESS_CODE + unit[i].len; ok && (a < end);) {
            walk(a, 0, &w);
            ok = (w.op_pc <= end);
            a  = insn_end(&w, end);
        }
        unplace();
    }
    return ok;
}

/* The instructions in a unit of instances. */
static int
count_insns(const ins_t *unit, int nunit)
{
    int    n = 0;
    walk_t w;

    for (int i = 0; i < nunit; i++) {
        place(unit[i].b, unit[i].len);
        for (uint32_t a = HARNESS_CODE, end = HARNESS_CODE + unit[i].len; a < end; n++) {
            walk(a, 0, &w);
            a = insn_end(&w, end);
        }
        unplace();
    }
    return n;
}

/* How an instruction leaves its block, by codegen_generate_call():
   B_NONE it does not; B_COND a conditional branch (Jcc, LOOPcc, JCXZ),
   which leaves on its taken path with the cycles so far and the held
   cycle; B_END one that always ends the block (JMP, CALL, RET imm, far
   RET, IRET, FF /2-/5), block_end() and its drain; B_EXIT an interpreter
   op that leaves right after its call on success (codegen_op_ends_block():
   INT3, INT n, INT1, POP SS, MOV SS, SYSENTER, SYSEXIT, the Cyrix SMINT
   forms, the K6's SYSCALL and SYSRET; INTO when it traps), with the cycles
   so far and the held cycle, before the block end would drain them. MOV
   SS is that only where the recompiler leaves it to its op; drive()
   decides. */
enum {
    B_NONE,
    B_COND,
    B_END,
    B_EXIT
};

static int
branch_kind(const walk_t *w)
{
    const uint8_t op = w->opcode;

    if (w->last_prefix == 0x0f) {
        if ((op & 0xf0) == 0x80)
            return B_COND;
        /* SYSENTER, SYSEXIT, the Cyrix SMINT forms, the K6's SYSCALL and
           SYSRET */
        if ((op == 0x34) || (op == 0x35) || (op == 0x3e) || (((op == 0x05) || (op == 0x07)) && is_k6) ||
            ((op == 0x7e) && !cpu_has_feature(CPU_FEATURE_MMX)))
            return B_EXIT;
        return B_NONE;
    }
    if (w->last_prefix)
        return B_NONE;
    if (((op & 0xf0) == 0x70) || ((op & 0xfc) == 0xe0))
        return B_COND;
    /* INT3, INT n, INT1, POP SS, MOV SS */
    if ((op == 0xcc) || (op == 0xcd) || (op == 0xf1) || (op == 0x17) || ((op == 0x8e) && ((w->fetchdat & 0x38) == 0x10)))
        return B_EXIT;
    if (op == 0xce)
        return B_COND; /* INTO: leaves only when OF is set */
    if ((op == 0xc2) || ((op & 0xfe) == 0xca) || (op == 0xcf) || ((op & 0xfc) == 0xe8))
        return B_END;
    if ((op == 0xff) && ((w->fetchdat & 0x38) >= 0x10) && ((w->fetchdat & 0x38) < 0x30))
        return B_END;
    return B_NONE;
}

/* An 0F opcode of the MMX tables (386_ops.h: the MMX rows of the Pentium
   MMX, Pentium II, K6 and 6x86MX 0F tables; the 6x86MX's EMMI row 50-5E;
   3DNow!'s 0F 0F), which reads and writes MMX registers; EMMS and FEMMS
   not among them. */
static int
mmx_op(uint8_t op)
{
    static const uint16_t row_d_f[3] = { 0xbb2e, 0xbb26, 0x772e };

    if (op == 0x0f)
        return cpu_has_feature(CPU_FEATURE_3DNOW);
    if ((op >= 0x50) && (op <= 0x5e))
        return (cpu_s->cpu_type == CPU_Cx6x86MX) && (op != 0x53) && (op != 0x56) && (op != 0x57);
    if (((op >= 0x60) && (op <= 0x6b)) || (op == 0x6e) || (op == 0x6f) || ((op >= 0x71) && (op <= 0x76)) || (op == 0x7e) ||
        (op == 0x7f))
        return 1;
    if (op >= 0xd0)
        return (row_d_f[(op >> 4) - 0xd] >> (op & 15)) & 1;
    return 0;
}

/* A row timed in real or virtual-8086 mode (cond rm, v86): MOV SS ends no
   block there and LMSW does (mov_ss(), rop0F01). drive_skipped: the last
   drives left an instruction to a POP SS's or MOV SS's op. */
static int drive_real;
static int drive_skipped;
/* drive_mark: a stream index, or -1; drive_mark_last: its instance ended
   its block (nothing after it shared the block). */
static int drive_mark = -1;
static int drive_mark_last;

/* What the compile pass leaves that decides where an op ends its block:
   the x87 TOP as the x87 ops move it, and whether MMX mode is on. A case
   starts with both 0 (harness.c run_case()). */
typedef struct dstate_t {
    x87_sim_t xs;
    int       ismmx;
} dstate_t;

/* An op that ends the block it is compiled in once it has run, beyond the
   branches branch_kind() names: the interpreter op the compile pass runs
   beside each compiled one calls CPU_BLOCK_END(), or the compiled form
   does (codegen_ops_*.c), or codegen_generate_call() ends the block after
   it. Near RET, far CALL (x86_ops_ret.h, x86_ops_call.h), HLT, INVD,
   WBINVD (x86_ops_misc.h), MOV CR0, CR3 and CR4 (rop0F22), INVLPG
   (rop0F01), LSS (ropLxS), LOADALL386 (0F 07; codegen.c), MOV DRx with the
   486 debug registers on (x86_ops_mov_ctrl.h), FXRSTOR (x86_ops_i686.h),
   FNINIT, and with the x87 ops (not softfloat, whose versions leave the
   block open) FNSAVE, FRSTOR and FLDENV, which reset or load TOP
   (x87_ops_misc.h). An MMX op ends it when it enters MMX mode from x87
   mode, or with TOP not 0 under the x87 ops (MMX_ENTER(),
   x86_ops_mmx.h); it leaves MMX mode on and TOP 0. Not here, as they do
   not end a block in the meter's protected-mode runs: LMSW (only in real
   mode: drive_real), FLDCW
   (only when it changes whether PC or RC is 0, x87_set_control_word(); its
   operand holds the control word the run starts with, x87_setup()), RSM
   (outside SMM it raises #UD), PUSHA and POPA (only past the register
   versions of a block: at most 8 stack reads each, 24 copies stay below
   them), LOOP (it unrolls only back to an instruction already in the
   block, codegen_can_unroll(); the runs set its displacement to 0, the
   next instruction), and a REP string op (only when ECX is
   above 1: the compile pass runs one element, x86_ops_rep_dyn.h; no row
   starts ECX above 1 with one, measure()). Updates the state. */
static int
op_ends_block(const walk_t *w, dstate_t *bs)
{
    const uint8_t op  = w->opcode;
    const int     reg = (w->fetchdat >> 3) & 7;

    if ((w->last_prefix & 0xf8) == 0xd8) {
        /* after an escape the walk's opcode byte is the ModR/M */
        const int x87_reg = (op >> 3) & 7;

        x87_effect(&bs->xs, w->last_prefix, op);
        if ((w->last_prefix == 0xdb) && (op == 0xe3)) {
            bs->ismmx = 0;
            return 1;
        }
        if ((op < 0xc0) && (w->last_prefix == 0xdd) && ((x87_reg == 4) || (x87_reg == 6)))
            bs->ismmx = 0;
        if ((op < 0xc0) && (((w->last_prefix == 0xdd) && ((x87_reg == 4) || (x87_reg == 6))) ||
                            ((w->last_prefix == 0xd9) && (x87_reg == 4))))
            return !fpu_softfloat;
        return 0;
    }
    if (w->last_prefix == 0x0f) {
        const int mem = ((w->fetchdat & 0xc0) != 0xc0);

        if ((op == 0x07) || (op == 0x08) || (op == 0x09) || ((op == 0x22) && ((reg == 0) || (reg == 3) || (reg == 4))) ||
            ((op == 0x23) && cpu_debug_regs_486) || ((op == 0x01) && (reg == 7) && mem) || ((op == 0xb2) && mem) ||
            ((op == 0x01) && (reg == 6) && drive_real))
            return 1;
        if ((op == 0xae) && ((w->fetchdat & 0xc0) != 0xc0) && (reg == 1) && (cpu_s->cpu_type == CPU_PENTIUM2D))
            return 1;
        if ((op == 0x77) || ((op == 0x0e) && cpu_has_feature(CPU_FEATURE_3DNOW))) {
            /* EMMS, FEMMS (x87_emms()) */
            bs->ismmx  = 0;
            bs->xs.top = 0;
            return 0;
        }
        if (cpu_has_feature(CPU_FEATURE_MMX) && mmx_op(op)) {
            const int end = !bs->ismmx || (!fpu_softfloat && (bs->xs.top & 7));

            bs->ismmx  = 1;
            bs->xs.top = 0;
            return end;
        }
        return 0;
    }
    return !w->last_prefix && ((op == 0xc3) || (op == 0x9a) || (op == 0xf4));
}

/* 1 when the instruction at addr cannot be compiled as the second of a MOV
   SS pair, as mov_ss_next_denied() (codegen_ops_mov.c) decides it: a byte
   up to its opcode (and an 8E or FF ModR/M) off the page the MOV SS starts
   on, an x87 escape, WAIT, any 0F instruction, and in protected mode also
   one compiled with the stack size (a push or pop, a call or return,
   ENTER, LEAVE, INT, IRET, another SS load). */
static int
mov_ss_next_denied(uint32_t page, uint32_t addr, int pmode)
{
    uint8_t op = 0;
    int     n;

    for (n = 0; n < 15; n++) {
        if (((addr + n) & ~0xfff) != page)
            return 1;
        op = ram[addr + n];
        if (!is_prefix_byte(op))
            break;
    }
    if (n == 15)
        return 1;
    if (((op & 0xf8) == 0xd8) || (op == 0x9b) || (op == 0x0f))
        return 1;
    if (!pmode)
        return 0;
    if ((op & 0xf0) == 0x50)
        return 1;
    switch (op) {
        case 0x06:
        case 0x07:
        case 0x0e:
        case 0x16:
        case 0x17:
        case 0x1e:
        case 0x1f:
        case 0x60:
        case 0x61:
        case 0x68:
        case 0x6a:
        case 0x8f:
        case 0x9a:
        case 0x9c:
        case 0x9d:
        case 0xc2:
        case 0xc3:
        case 0xca:
        case 0xcb:
        case 0xc8:
        case 0xc9:
        case 0xcc:
        case 0xcd:
        case 0xce:
        case 0xcf:
        case 0xe8:
            return 1;
        case 0x8e:
        case 0xff:
            if (((addr + n + 1) & ~0xfff) != page)
                return 1;
            if (op == 0x8e)
                return (ram[addr + n + 1] & 0x38) == 0x10;
            return ((ram[addr + n + 1] & 0x38) == 0x10) || ((ram[addr + n + 1] & 0x38) == 0x18) ||
                   ((ram[addr + n + 1] & 0x38) == 0x30);
        default:
            return 0;
    }
}

/* A relative branch: its displacement is the bytes after the opcode. */
static int
is_relative(const walk_t *w)
{
    const uint8_t op = w->opcode;

    return (!w->last_prefix && (((op & 0xf0) == 0x70) || ((op & 0xfc) == 0xe0) || (op == 0xe8) || (op == 0xe9) || (op == 0xeb))) ||
           ((w->last_prefix == 0x0f) && ((op & 0xf0) == 0x80));
}

/* The instance the end-to-end run times: a relative branch with its
   displacement set to 0, so its target is the next instruction (no model
   reads the displacement; the opcode and the length stay). Returns -1 for
   a form that cannot be kept in the body (far, indirect, return, an
   interrupt, INTO when it traps), whose handler or target would leave the
   copies. */
static int
wire_form(const ins_t *in, int taken, ins_t *out, walk_t *wout)
{
    walk_t  w;
    uint8_t op;

    *out = *in;
    place(in->b, in->len);
    walk(HARNESS_CODE, 0, &w);
    unplace();
    if (wout)
        *wout = w;
    op = w.opcode;
    if (is_relative(&w)) {
        for (uint32_t a = w.op_pc - HARNESS_CODE; a < (uint32_t) in->len; a++)
            out->b[a] = 0;
        return 0;
    }
    if (!w.last_prefix && (op == 0xce))
        return taken ? -1 : 0;
    /* POP SS and MOV SS leave the block, not the body. */
    if (!w.last_prefix && ((op == 0x17) || (op == 0x8e)))
        return 0;
    if ((branch_kind(&w) != B_NONE) || (!w.last_prefix && ((op == 0xc3) || (op == 0x9a))))
        return -1;
    return 0;
}

/* The flags and ECX that put a conditional branch, CMOVcc or SETcc (its
   condition taken or not) on the row's path, or -1 where no start state
   does for every copy (LOOP, LOOPE, LOOPNE not taken: a copy after the
   first finds ECX counted down past 0). */
static int
path_state(const walk_t *w, int taken, uint32_t *eflags, uint32_t *ecx)
{
    const uint8_t op = w->opcode;
    int           cc;
    int           want;

    *eflags = 0x0002;
    *ecx    = 1;
    if (!w->last_prefix && (op == 0xce)) {
        if (taken)
            *eflags |= V_FLAG;
        return 0;
    }
    if (!w->last_prefix && ((op & 0xfc) == 0xe0)) {
        if (op == 0xe3) {
            *ecx = taken ? 0 : 1; /* JCXZ */
            return 0;
        }
        if (!taken)
            return -1;
        *ecx = 1000; /* LOOP, LOOPE, LOOPNE taken */
        if (op == 0xe1)
            *eflags |= Z_FLAG;
        return 0;
    }
    cc = op & 0x0f;
    /* The even condition of the pair (O, B, E, BE, S, P, L, LE) is false
       with every flag clear, and true with its own flag set. */
    want = taken ? !(cc & 1) : (cc & 1);
    if (want) {
        static const uint32_t set[8] = { V_FLAG, C_FLAG, Z_FLAG, C_FLAG, N_FLAG, P_FLAG, N_FLAG, Z_FLAG };

        *eflags |= set[cc >> 1];
    }
    return 0;
}

/* The taken path's charge (codegen.c, after the opcode hook). */
static int
taken_cycles(const walk_t *w)
{
    if (codegen_timing_jump_cycles)
        return codegen_timing_jump_cycles();
    if (((w->last_prefix == 0) && ((w->opcode & 0xf0) == 0x70)) || ((w->last_prefix == 0x0f) && ((w->opcode & 0xf0) == 0x80)))
        return timing_bt;
    return 0;
}

/* The held cycle an op that leaves after its call charges (codegen.c,
   before the call): the model's jump_cycles(), none without the hook. */
static int
exit_cycles(void)
{
    return codegen_timing_jump_cycles ? codegen_timing_jump_cycles() : 0;
}

/* Drives the hooks over a stream of instances, instruction by instruction
   (insn_end()), from the model's cleared state, splitting blocks where the
   recompiler splits them: at its instruction limit, after B_END and after
   an op that ends its block (op_ends_block()), block_end() and its drain,
   as codegen_block_end_recompile() charges them (the held cycle codegen
   adds and refunds around a branch nets to nothing); at a taken B_COND
   (taken: the row's are taken) and at B_EXIT, the exit's charge, the cycles
   so far and the held cycle, with block_end() run for the model's state
   but its drain not charged. STI ends its block after the instruction
   that follows it (its op sets cpu_end_block_after_ins, which the compile
   loop counts down, 386_dynarec.c). MOV SS (protected mode, the harness's)
   is compiled with the instruction after it as a pair in its block, no
   held cycle, the block ending after that instruction (mov_ss(),
   codegen_ops_mov.c), where that instruction may be paired
   (mov_ss_next_denied()) and the block has room for it; else it is left
   to its op, B_EXIT, as POP SS always is, and that op runs the next
   instruction itself: no hook sees it, and the next block starts after it.
   The register-version limit mov_ss() also checks is not followed: a row
   that reaches it reads GAP. The next block starts with block_start() on
   the state block_end() left, as the next compile finds it. ctx: the
   stream is timed up to its last instance (the epilogue's time read), with
   no block_end() after it; else the last block ends with block_end().
   What a run before left pending in the model (the K6's decode buffer,
   which neither block_start() nor a load empties) is drained uncharged
   first, and what this one leaves at the time read after it. Returns 0, or
   -1 on a fatal() from the model. */
static int
drive(const ins_t *const *stream, int nstream, int taken, int ctx, int64_t *total)
{
    static uint8_t  buf[BUF_MAX];
    static uint32_t starts[STREAM_MAX * UNIT_MAX];
    static uint32_t ends[STREAM_MAX * UNIT_MAX];
    int             len   = 0;
    int64_t         sum   = 0;
    int             in_bl = 0;
    int             pend  = 0; /* STI, a compiled MOV SS: the block ends after the instruction that follows */
    int             skip  = 0; /* POP SS, a MOV SS left to its op: that op runs the instruction that follows */
    walk_t          w;
    dstate_t        bs;

    memset(&bs, 0, sizeof(bs));
    for (int i = 0; i < nstream; i++) {
        if (len + stream[i]->len > BUF_MAX - 16)
            return -1;
        starts[i] = HARNESS_CODE + len;
        memcpy(buf + len, stream[i]->b, stream[i]->len);
        len += stream[i]->len;
        ends[i] = HARNESS_CODE + len;
    }
    place(buf, len);
    harness_fatal_hook = meter_fatal;
    if (setjmp(meter_jmp)) {
        harness_fatal_hook = NULL;
        unplace();
        return -1;
    }
    codegen_timing_block_end();
    if (codegen_timing_load)
        codegen_timing_load(NULL);
    codegen_block_cycles = 0;
    codegen_timing_block_start();
    for (int i = 0; i < nstream; i++) {
        for (uint32_t a = starts[i]; a < ends[i];) {
            const uint32_t at = a;
            int            bk;

            if (skip) {
                /* the op runs it; a POP SS or MOV SS run so runs the one
                   after it in turn */
                drive_skipped = 1;
                walk(a, 0, &w);
                a    = insn_end(&w, ends[i]);
                skip = !w.last_prefix && ((w.opcode == 0x17) || ((w.opcode == 0x8e) && ((w.fetchdat & 0x38) == 0x10)));
                continue;
            }
            if (in_bl == MAX_INSTRUCTION_COUNT) {
                codegen_timing_block_end();
                sum += codegen_block_cycles;
                codegen_block_cycles = 0;
                codegen_timing_block_start();
                in_bl = 0;
                pend  = 0;
            }
            walk(a, 1, &w);
            a = insn_end(&w, ends[i]);
            in_bl++;
            bk = branch_kind(&w);
            if (!w.last_prefix && (w.opcode == 0x8e) && ((w.fetchdat & 0x38) == 0x10)) {
                if ((in_bl < MAX_INSTRUCTION_COUNT) && !mov_ss_next_denied(at & ~0xfff, a, !drive_real)) {
                    bk   = B_NONE;
                    pend = drive_real ? 0 : 2;
                } else
                    skip = 1;
            } else if (!w.last_prefix && (w.opcode == 0x17))
                skip = 1;
            else if (!w.last_prefix && (w.opcode == 0xfb))
                pend = 2;
            if ((bk == B_NONE) && op_ends_block(&w, &bs))
                bk = B_END;
            if (pend && !--pend && (bk == B_NONE))
                bk = B_END;
            if (bk == B_END) {
                codegen_timing_block_end();
                sum += codegen_block_cycles;
                codegen_block_cycles = 0;
                codegen_timing_block_start();
                in_bl = 0;
                pend  = 0;
            } else if (((bk == B_COND) && taken) || (bk == B_EXIT)) {
                sum += codegen_block_cycles + ((bk == B_EXIT) ? exit_cycles() : taken_cycles(&w));
                codegen_block_cycles = 0;
                codegen_timing_block_end();
                codegen_block_cycles = 0;
                codegen_timing_block_start();
                in_bl = 0;
                pend  = 0;
            }
        }
        if (i == drive_mark)
            drive_mark_last = ((in_bl == 0) || (in_bl == MAX_INSTRUCTION_COUNT)) && !skip;
    }
    if (!ctx)
        codegen_timing_block_end();
    sum += codegen_block_cycles;
    *total = sum;
    if (ctx)
        codegen_timing_block_end();
    codegen_block_cycles = 0;
    harness_fatal_hook   = NULL;
    unplace();
    return 0;
}

/* ---- the model's table ----------------------------------------------- */

static const char     *model_name = "unknown";
static meter_struct_fn model_struct;

static void
model_identify(void)
{
    static const struct {
        codegen_timing_t *t;
        const char       *name;
        meter_struct_fn   fn;
    } models[] = {
        { &codegen_timing_486, "486", meter_struct_486 },
        { &codegen_timing_pentium, "pentium", meter_struct_pentium },
        { &codegen_timing_686, "686", meter_struct_686 },
        { &codegen_timing_p6, "p6", meter_struct_p6 },
        { &codegen_timing_k6, "k6", meter_struct_k6 },
        { &codegen_timing_winchip, "winchip", meter_struct_winchip },
        { &codegen_timing_winchip2, "winchip2", meter_struct_winchip2 }
    };

    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        if (codegen_timing_opcode == models[i].t->opcode) {
            model_name   = models[i].name;
            model_struct = models[i].fn;
            return;
        }
    }
}

static int kv_get(const char *list, const char *key, char *v, size_t n);

/* The pairing the Pentium model performs for an instruction, beside its
   table's class: codegen_timing_pentium_prefix() adds to the decode delay
   for each prefix but the escapes, a 0F ahead of a Jcc and, with MMX, any
   0F (66 and 67 then add 2), and the opcode hook pairs an instruction in
   the U or the V pipe only when that delay is shadowed (decode_delay +
   decode_delay_offset <= 0), which only extra cycles of an earlier
   instruction do. With a delay the instance's pair reads NP,
   pair_table the table's class and prefix_delay the delay. */
static void
pentium_pairing(const ins_t *in, char *out, size_t n)
{
    char    pair[32];
    char    rest[512];
    char   *p;
    int     delay = 0;
    int     k     = 0;
    walk_t  w;
    ins_t   h;

    past_fwait(in, &h);
    place(h.b, h.len);
    walk(HARNESS_CODE, 0, &w);
    unplace();
    for (k = 0; k < w.nprefix; k++) {
        const uint8_t b = h.b[k];

        if ((b & 0xf8) == 0xd8)
            continue;
        if (cpu_has_feature(CPU_FEATURE_MMX) && (b == 0x0f))
            continue;
        if (cpu_has_feature(CPU_FEATURE_MMX) && ((b == 0x66) || (b == 0x67))) {
            delay += 2;
            continue;
        }
        if ((b == 0x0f) && (k + 1 < h.len) && ((h.b[k + 1] & 0xf0) == 0x80))
            continue;
        delay++;
    }
    if (!delay || !kv_get(out, "pair", pair, sizeof(pair)) || (strcmp(pair, "U") && strcmp(pair, "V") && strcmp(pair, "UV")))
        return;
    p = strstr(out, "pair=");
    snprintf(rest, sizeof(rest), "%s", p + 5 + strlen(pair));
    snprintf(p, n - (p - out), "pair=NP%s;pair_table=%s;prefix_delay=%d", rest, pair, delay);
}

static void
read_struct(const ins_t *in, char *out, size_t n, walk_t *w)
{
    out[0] = 0;
    place(in->b, in->len);
    walk(HARNESS_CODE, 0, w);
    if (!model_struct) {
        snprintf(out, n, "-");
        unplace();
        return;
    }
    harness_fatal_hook = meter_fatal;
    if (setjmp(meter_jmp)) {
        harness_fatal_hook = NULL;
        unplace();
        snprintf(out, n, "fatal=%s", meter_fatal_msg);
        return;
    }
    model_struct(w->last_prefix, w->opcode, w->fetchdat, w->op_32, w->op_pc, w->nprefix, out, n);
    harness_fatal_hook = NULL;
    unplace();
    if (!strcmp(model_name, "pentium"))
        pentium_pairing(in, out, n);
}

/* The interpreter-side charge an instruction reaches, by the same name on
   every CPU, so the SHARED report can put the classes side by side: the x87
   table cpu_set() chose, the REP element charges, the branch and far
   transfer timing_* variables, else the interpreter's op. */
static void
interp_row(const ins_t *in, const walk_t *w, char *out, size_t n)
{
    const uint8_t op    = w->opcode;
    const int     mod3  = ((w->fetchdat & 0xc0) == 0xc0);
    const int     digit = (w->fetchdat >> 3) & 7;
    int           rep   = 0;

    for (int i = 0; (i < in->len) && is_prefix_byte(in->b[i]); i++) {
        if ((in->b[i] == 0xf2) || (in->b[i] == 0xf3))
            rep = in->b[i];
    }
    if ((w->last_prefix & 0xf8) == 0xd8) {
        snprintf(out, n, "%s:%02x/%s", meter_x87_table_name(), w->last_prefix, "");
        if (mod3)
            meter_putf(out, n, "%02x", op);
        else
            meter_putf(out, n, "m%d", (op >> 3) & 7);
        return;
    }
    if (!w->last_prefix && rep &&
        (((op >= 0xa4) && (op <= 0xa7)) || ((op >= 0xaa) && (op <= 0xaf)) || ((op >= 0x6c) && (op <= 0x6f)))) {
        snprintf(out, n, "x86_ops_rep:%02x.%02x", rep, op);
        return;
    }
    if ((!w->last_prefix && ((op & 0xf0) == 0x70)) || ((w->last_prefix == 0x0f) && ((op & 0xf0) == 0x80))) {
        snprintf(out, n, "timing_bt+timing_bnt:jcc");
        return;
    }
    if (!w->last_prefix && ((op == 0x9a) || (op == 0xea) || (op == 0xca) || (op == 0xcb) || (op == 0xcc) || (op == 0xcd) ||
                            (op == 0xce) || (op == 0xcf) || ((op == 0xff) && ((digit == 3) || (digit == 5))))) {
        snprintf(out, n, "timing_far:%02x", op);
        if (op == 0xff)
            meter_putf(out, n, "/%d", digit);
        return;
    }
    snprintf(out, n, "interp:%s%02x", (w->last_prefix == 0x0f) ? "0f." : "", op);
    if (!w->last_prefix) {
        switch (op) {
            case 0x80:
            case 0x81:
            case 0x82:
            case 0x83:
            case 0x8f:
            case 0xc0:
            case 0xc1:
            case 0xc6:
            case 0xc7:
            case 0xd0:
            case 0xd1:
            case 0xd2:
            case 0xd3:
            case 0xf6:
            case 0xf7:
            case 0xfe:
            case 0xff:
                meter_putf(out, n, "/%d", digit);
                break;
            default:
                break;
        }
    } else if ((op == 0x00) || (op == 0x01) || (op == 0xba) || (op == 0xc7))
        meter_putf(out, n, "/%d", digit);
    meter_putf(out, n, "%s", mod3 ? "r" : "m");
}

/* ---- what an instruction reads and writes ---------------------------- */

/* The registers and flags an instruction reads and writes, from its own
   definition (the Intel and AMD instruction set references: operands,
   implicit registers, the flags it tests and the flags it sets or leaves
   undefined, which count as written), never from a timing model's tables:
   bits 0-7 the general registers (a byte or word register counts as its
   whole register), 8-15 the MMX registers, 16-23 the x87 registers by
   physical index, 24-29 CF, PF, AF, ZF, SF and OF, 30 DF, 31 IF. Memory and
   the segment registers are not followed. */
#define RW_GPR(n) (1u << (n))
#define RW_MM(n)  (0x100u << (n))
#define RW_ST(n)  (0x10000u << ((n) & 7))
#define RW_CF     (1u << 24)
#define RW_PF     (1u << 25)
#define RW_AF     (1u << 26)
#define RW_ZF     (1u << 27)
#define RW_SF     (1u << 28)
#define RW_OF     (1u << 29)
#define RW_DF     (1u << 30)
#define RW_IF     (1u << 31)
#define RW_ARITH  (RW_CF | RW_PF | RW_AF | RW_ZF | RW_SF | RW_OF)
#define RW_EAX    RW_GPR(0)
#define RW_ECX    RW_GPR(1)
#define RW_EDX    RW_GPR(2)
#define RW_EBX    RW_GPR(3)
#define RW_ESP    RW_GPR(4)
#define RW_EBP    RW_GPR(5)
#define RW_ESI    RW_GPR(6)
#define RW_EDI    RW_GPR(7)

/* rd and wr as above; xch the physical x87 register an FXCH swaps with
   ST(0), else -1: FXCH moves two values and makes none. */
typedef struct rw_t {
    uint32_t rd;
    uint32_t wr;
    int      xch;
} rw_t;

/* The registers a ModR/M memory operand's address reads (fetchdat holds
   the ModR/M byte, then the SIB byte). */
static uint32_t
addr_regs(uint32_t fetchdat, int a32)
{
    static const uint8_t r16[8] = { 0x48, 0x88, 0x60, 0xa0, 0x40, 0x80, 0x20, 0x08 };
    const int            mod    = (fetchdat >> 6) & 3;
    const int            rm     = fetchdat & 7;

    if (mod == 3)
        return 0;
    if (!a32)
        return ((mod == 0) && (rm == 6)) ? 0 : r16[rm];
    if (rm == 4) {
        const int sib = (fetchdat >> 8) & 0xff;
        uint32_t  r   = 0;

        if (((sib >> 3) & 7) != 4)
            r |= RW_GPR((sib >> 3) & 7);
        if (((sib & 7) != 5) || mod)
            r |= RW_GPR(sib & 7);
        return r;
    }
    return ((mod == 0) && (rm == 5)) ? 0 : RW_GPR(rm);
}

/* The flags a condition code (Jcc, SETcc, CMOVcc) tests. */
static uint32_t
cc_flags(int cc)
{
    static const uint32_t f[8] = { RW_OF, RW_CF, RW_ZF, RW_CF | RW_ZF, RW_SF, RW_PF, RW_SF | RW_OF, RW_ZF | RW_SF | RW_OF };

    return f[(cc >> 1) & 7];
}

/* An integer instruction (one-byte or 0F table) at a, as walk() found it. */
static void
int_rw(const walk_t *w, uint32_t a, rw_t *o)
{
    const uint8_t  op   = w->opcode;
    const uint32_t f    = w->fetchdat;
    const int      mod3 = ((f & 0xc0) == 0xc0);
    const int      reg  = (f >> 3) & 7;
    const int      rm   = f & 7;
    const uint32_t ea   = addr_regs(f, !!(w->op_32 & 0x200));
    int            byte = 0;
    int            rep  = 0;
    uint32_t       rd   = 0;
    uint32_t       wr   = 0;

    for (uint32_t p = a; p + 1 < w->op_pc; p++) {
        if ((ram[p] == 0xf2) || (ram[p] == 0xf3))
            rep = 1;
    }
    /* the reg operand, the r/m operand as a register (0 for memory), and
       the r/m operand as read (its address registers for memory) */
#define RREG  RW_GPR(byte ? (reg & 3) : reg)
#define RRM   (mod3 ? RW_GPR(byte ? (rm & 3) : rm) : 0)
#define RRMRD (mod3 ? RRM : ea)
#define RRMWA (mod3 ? 0 : ea) /* a destination r/m: its address is read */
    if (w->last_prefix == 0x0f) {
        switch (op) {
            case 0x00:
                if (reg < 2) {
                    rd = RRMWA;
                    wr = RRM;
                } else if (reg < 4)
                    rd = RRMRD;
                else {
                    rd = RRMRD;
                    wr = RW_ZF;
                }
                break;
            case 0x01:
                if ((reg == 4) || (reg == 6)) {
                    rd = (reg == 4) ? RRMWA : RRMRD;
                    wr = (reg == 4) ? RRM : 0;
                } else
                    rd = ea;
                break;
            case 0x02: /* LAR, LSL */
            case 0x03:
                rd = RRMRD;
                wr = RREG | RW_ZF;
                break;
            case 0x20: /* MOV r32,CRn / DRn / TRn */
            case 0x21:
            case 0x24:
                wr = RW_GPR(rm);
                break;
            case 0x22:
            case 0x23:
            case 0x26:
                rd = RW_GPR(rm);
                break;
            case 0x30: /* WRMSR */
                rd = RW_ECX | RW_EAX | RW_EDX;
                break;
            case 0x31: /* RDTSC */
                wr = RW_EAX | RW_EDX;
                break;
            case 0x32: /* RDMSR, RDPMC */
            case 0x33:
                rd = RW_ECX;
                wr = RW_EAX | RW_EDX;
                break;
            case 0xa0: /* PUSH FS, POP FS, PUSH GS, POP GS */
            case 0xa1:
            case 0xa8:
            case 0xa9:
                rd = wr = RW_ESP;
                break;
            case 0xa2: /* CPUID */
                rd = RW_EAX | RW_ECX;
                wr = RW_EAX | RW_EBX | RW_ECX | RW_EDX;
                break;
            case 0xa3: /* BT */
                rd = RREG | RRMRD;
                wr = RW_CF;
                break;
            case 0xab: /* BTS, BTR, BTC */
            case 0xb3:
            case 0xbb:
                rd = RREG | RRMRD;
                wr = RRM | RW_CF;
                break;
            case 0xa4: /* SHLD, SHRD by an immediate, by CL */
            case 0xac:
            case 0xa5:
            case 0xad:
                rd = RREG | RRMRD | ((op & 1) ? RW_ECX : 0);
                wr = RRM | RW_ARITH;
                break;
            case 0xaf: /* IMUL r,r/m */
                rd = RREG | RRMRD;
                wr = RREG | RW_ARITH;
                break;
            case 0xb0: /* CMPXCHG */
            case 0xb1:
                byte = !(op & 1);
                rd   = RREG | RRMRD | RW_EAX;
                wr   = RRM | RW_EAX | RW_ARITH;
                break;
            case 0xb2: /* LSS, LFS, LGS */
            case 0xb4:
            case 0xb5:
                rd = ea;
                wr = RREG;
                break;
            case 0xb6: /* MOVZX, MOVSX: a byte or word source */
            case 0xb7:
            case 0xbe:
            case 0xbf:
                wr   = RREG;
                byte = !(op & 1);
                rd   = RRMRD;
                break;
            case 0xba: /* BT, BTS, BTR, BTC r/m,imm8 */
                rd = RRMRD;
                wr = ((reg > 4) ? RRM : 0) | RW_CF;
                break;
            case 0xbc: /* BSF, BSR */
            case 0xbd:
                rd = RRMRD;
                wr = RREG | RW_ARITH;
                break;
            case 0xc0: /* XADD */
            case 0xc1:
                byte = !(op & 1);
                rd   = RREG | RRMRD;
                wr   = RREG | RRM | RW_ARITH;
                break;
            case 0xc7: /* CMPXCHG8B */
                if (reg == 1) {
                    rd = ea | RW_EAX | RW_EBX | RW_ECX | RW_EDX;
                    wr = RW_EAX | RW_EDX | RW_ZF;
                }
                break;
            default:
                if ((op & 0xf0) == 0x40) { /* CMOVcc */
                    rd = RREG | RRMRD | cc_flags(op);
                    wr = RREG;
                } else if ((op & 0xf0) == 0x80) /* Jcc */
                    rd = cc_flags(op);
                else if ((op & 0xf0) == 0x90) { /* SETcc */
                    byte = 1;
                    rd   = cc_flags(op) | RRMWA;
                    wr   = RRM;
                } else if (op >= 0xc8) /* BSWAP */
                    rd = wr = RW_GPR(op & 7);
                else if ((op == 0x0d) || (op == 0x18) || (op == 0xae)) /* prefetches, FXSAVE, FXRSTOR */
                    rd = ea;
                break;
        }
        goto out;
    }
    if ((op < 0x40) && ((op & 7) < 6)) { /* ADD, OR, ADC, SBB, AND, SUB, XOR, CMP */
        const int alu = op >> 3;

        byte = !(op & 1);
        switch (op & 7) {
            case 0:
            case 1:
                rd = RREG | RRMRD;
                wr = (alu != 7) ? RRM : 0;
                break;
            case 2:
            case 3:
                rd = RREG | RRMRD;
                wr = (alu != 7) ? RREG : 0;
                break;
            default:
                rd = RW_EAX;
                wr = (alu != 7) ? RW_EAX : 0;
                break;
        }
        if ((alu == 2) || (alu == 3))
            rd |= RW_CF;
        wr |= RW_ARITH;
        goto out;
    }
    if ((op >= 0x40) && (op <= 0x4f)) { /* INC, DEC */
        rd = RW_GPR(op & 7);
        wr = RW_GPR(op & 7) | (RW_ARITH & ~RW_CF);
        goto out;
    }
    if ((op >= 0x50) && (op <= 0x57)) {
        rd = RW_GPR(op & 7) | RW_ESP;
        wr = RW_ESP;
        goto out;
    }
    if ((op >= 0x58) && (op <= 0x5f)) {
        rd = RW_ESP;
        wr = RW_GPR(op & 7) | RW_ESP;
        goto out;
    }
    if ((op & 0xf0) == 0x70) {
        rd = cc_flags(op);
        goto out;
    }
    if ((op >= 0x91) && (op <= 0x97)) {
        rd = wr = RW_EAX | RW_GPR(op & 7);
        goto out;
    }
    if ((op >= 0xb0) && (op <= 0xbf)) {
        wr = RW_GPR((op < 0xb8) ? (op & 3) : (op & 7));
        goto out;
    }
    switch (op) {
        case 0x06: /* PUSH and POP of a segment register */
        case 0x07:
        case 0x0e:
        case 0x16:
        case 0x17:
        case 0x1e:
        case 0x1f:
        case 0x68: /* PUSH imm */
        case 0x6a:
        case 0x9a: /* CALL far, RET, RETF */
        case 0xc2:
        case 0xc3:
        case 0xca:
        case 0xcb:
        case 0xe8: /* CALL */
            rd = wr = RW_ESP;
            break;
        case 0x27: /* DAA, DAS */
        case 0x2f:
            rd = RW_EAX | RW_CF | RW_AF;
            wr = RW_EAX | RW_ARITH;
            break;
        case 0x37: /* AAA, AAS */
        case 0x3f:
            rd = RW_EAX | RW_AF;
            wr = RW_EAX | RW_ARITH;
            break;
        case 0x60: /* PUSHA, POPA */
            rd = 0xff;
            wr = RW_ESP;
            break;
        case 0x61:
            rd = RW_ESP;
            wr = 0xff;
            break;
        case 0x62: /* BOUND */
            rd = RREG | ea;
            break;
        case 0x63: /* ARPL */
            rd = RREG | RRMRD;
            wr = RRM | RW_ZF;
            break;
        case 0x69: /* IMUL r,r/m,imm */
        case 0x6b:
            rd = RRMRD;
            wr = RREG | RW_ARITH;
            break;
        case 0x6c: /* INS, OUTS */
        case 0x6d:
            rd = RW_EDX | RW_EDI | RW_DF;
            wr = RW_EDI;
            break;
        case 0x6e:
        case 0x6f:
            rd = RW_EDX | RW_ESI | RW_DF;
            wr = RW_ESI;
            break;
        case 0x80: /* ALU r/m,imm */
        case 0x81:
        case 0x82:
        case 0x83:
            byte = (op == 0x80) || (op == 0x82);
            rd   = RRMRD | (((reg == 2) || (reg == 3)) ? RW_CF : 0);
            wr   = ((reg != 7) ? RRM : 0) | RW_ARITH;
            break;
        case 0x84: /* TEST */
        case 0x85:
            byte = !(op & 1);
            rd   = RREG | RRMRD;
            wr   = RW_ARITH;
            break;
        case 0x86: /* XCHG */
        case 0x87:
            byte = !(op & 1);
            rd   = RREG | RRMRD;
            wr   = RREG | RRM;
            break;
        case 0x88: /* MOV r/m,r */
        case 0x89:
            byte = !(op & 1);
            rd   = RREG | RRMWA;
            wr   = RRM;
            break;
        case 0x8a: /* MOV r,r/m */
        case 0x8b:
            byte = !(op & 1);
            rd   = RRMRD;
            wr   = RREG;
            break;
        case 0x8c: /* MOV r/m,Sreg */
            rd = RRMWA;
            wr = RRM;
            break;
        case 0x8d: /* LEA */
            rd = ea;
            wr = RREG;
            break;
        case 0x8e: /* MOV Sreg,r/m */
            rd = RRMRD;
            break;
        case 0x8f: /* POP r/m */
            rd = RW_ESP | RRMWA;
            wr = RW_ESP | RRM;
            break;
        case 0x98: /* CBW, CWDE */
            rd = wr = RW_EAX;
            break;
        case 0x99: /* CWD, CDQ */
            rd = RW_EAX;
            wr = RW_EDX;
            break;
        case 0x9c: /* PUSHF */
            rd = RW_ARITH | RW_DF | RW_IF | RW_ESP;
            wr = RW_ESP;
            break;
        case 0x9d: /* POPF */
            rd = RW_ESP;
            wr = RW_ARITH | RW_DF | RW_IF | RW_ESP;
            break;
        case 0x9e: /* SAHF */
            rd = RW_EAX;
            wr = RW_ARITH & ~RW_OF;
            break;
        case 0x9f: /* LAHF */
            rd = RW_ARITH & ~RW_OF;
            wr = RW_EAX;
            break;
        case 0xa0: /* MOV AL/eAX, moffs and back */
        case 0xa1:
            wr = RW_EAX;
            break;
        case 0xa2:
        case 0xa3:
            rd = RW_EAX;
            break;
        case 0xa4: /* MOVS */
        case 0xa5:
            rd = wr = RW_ESI | RW_EDI;
            rd |= RW_DF;
            break;
        case 0xa6: /* CMPS */
        case 0xa7:
            rd = wr = RW_ESI | RW_EDI;
            rd |= RW_DF;
            wr |= RW_ARITH;
            break;
        case 0xa8: /* TEST AL/eAX,imm */
        case 0xa9:
            rd = RW_EAX;
            wr = RW_ARITH;
            break;
        case 0xaa: /* STOS */
        case 0xab:
            rd = RW_EAX | RW_EDI | RW_DF;
            wr = RW_EDI;
            break;
        case 0xac: /* LODS */
        case 0xad:
            rd = RW_ESI | RW_DF;
            wr = RW_EAX | RW_ESI;
            break;
        case 0xae: /* SCAS */
        case 0xaf:
            rd = RW_EAX | RW_EDI | RW_DF;
            wr = RW_EDI | RW_ARITH;
            break;
        case 0xc0: /* shifts and rotates */
        case 0xc1:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3:
            byte = !(op & 1);
            rd   = RRMRD | (((op & 0xfe) == 0xd2) ? RW_ECX : 0) | (((reg == 2) || (reg == 3)) ? RW_CF : 0);
            wr   = RRM | ((reg < 4) ? (RW_CF | RW_OF) : RW_ARITH);
            break;
        case 0xc4: /* LES, LDS */
        case 0xc5:
            rd = ea;
            wr = RREG;
            break;
        case 0xc6: /* MOV r/m,imm */
        case 0xc7:
            byte = !(op & 1);
            rd   = RRMWA;
            wr   = RRM;
            break;
        case 0xc8: /* ENTER */
            rd = wr = RW_ESP | RW_EBP;
            break;
        case 0xc9: /* LEAVE */
            rd = RW_EBP;
            wr = RW_ESP | RW_EBP;
            break;
        case 0xcc: /* INT3, INT, INTO, INT1 */
        case 0xcd:
        case 0xce:
        case 0xf1:
            rd = RW_ARITH | RW_DF | RW_IF | RW_ESP;
            wr = RW_ESP | RW_IF;
            break;
        case 0xcf: /* IRET */
            rd = RW_ESP;
            wr = RW_ESP | RW_ARITH | RW_DF | RW_IF;
            break;
        case 0xd4: /* AAM, AAD */
        case 0xd5:
            rd = RW_EAX;
            wr = RW_EAX | RW_ARITH;
            break;
        case 0xd6: /* SALC */
            rd = RW_CF;
            wr = RW_EAX;
            break;
        case 0xd7: /* XLAT */
            rd = RW_EAX | RW_EBX;
            wr = RW_EAX;
            break;
        case 0xe0: /* LOOPNE, LOOPE, LOOP */
        case 0xe1:
        case 0xe2:
            rd = RW_ECX | ((op != 0xe2) ? RW_ZF : 0);
            wr = RW_ECX;
            break;
        case 0xe3: /* JCXZ */
            rd = RW_ECX;
            break;
        case 0xe4: /* IN, OUT */
        case 0xe5:
            wr = RW_EAX;
            break;
        case 0xe6:
        case 0xe7:
            rd = RW_EAX;
            break;
        case 0xec:
        case 0xed:
            rd = RW_EDX;
            wr = RW_EAX;
            break;
        case 0xee:
        case 0xef:
            rd = RW_EDX | RW_EAX;
            break;
        case 0xf5: /* CMC */
            rd = wr = RW_CF;
            break;
        case 0xf6: /* TEST, NOT, NEG, MUL, IMUL, DIV, IDIV */
        case 0xf7:
            byte = !(op & 1);
            rd   = RRMRD;
            if (reg < 2)
                wr = RW_ARITH;
            else if (reg == 2)
                wr = RRM;
            else if (reg == 3)
                wr = RRM | RW_ARITH;
            else {
                rd |= RW_EAX | (((reg >= 6) && !byte) ? RW_EDX : 0);
                wr = RW_EAX | (byte ? 0 : RW_EDX) | RW_ARITH;
            }
            break;
        case 0xf8: /* CLC, STC */
        case 0xf9:
            wr = RW_CF;
            break;
        case 0xfa: /* CLI, STI */
        case 0xfb:
            wr = RW_IF;
            break;
        case 0xfc: /* CLD, STD */
        case 0xfd:
            wr = RW_DF;
            break;
        case 0xfe: /* INC, DEC r/m */
        case 0xff:
            byte = (op == 0xfe);
            if (reg < 2) {
                rd = RRMRD;
                wr = RRM | (RW_ARITH & ~RW_CF);
            } else if ((reg == 2) || (reg == 6)) { /* CALL, PUSH r/m */
                rd = RRMRD | RW_ESP;
                wr = RW_ESP;
            } else if (reg == 3) { /* CALL far */
                rd = ea | RW_ESP;
                wr = RW_ESP;
            } else if (reg == 4) /* JMP r/m */
                rd = RRMRD;
            else if (reg == 5)
                rd = ea;
            break;
        default:
            break;
    }
out:
    if (rep && !w->last_prefix && (((op >= 0x6c) && (op <= 0x6f)) || ((op >= 0xa4) && (op <= 0xa7)) || ((op >= 0xaa) && (op <= 0xaf)))) {
        rd |= RW_ECX;
        wr |= RW_ECX;
    }
#undef RREG
#undef RRM
#undef RRMRD
#undef RRMWA
    o->rd  = rd;
    o->wr  = wr;
    o->xch = -1;
}

/* An x87 instruction (escape op, ModR/M modrm): rd the ST(i) it reads (bit
   i, before it runs), wr the ST(i) it writes (bit i, after its pushes and
   pops), xch the ST(i) FXCH swaps with ST(0) (else -1), fr and fw the
   flags it tests and sets, gw the general registers it writes. As each
   book's FPU instruction pages state the stack: a load pushes and writes
   the new ST(0); FPTAN, FXTRACT and FSINCOS write ST(0) and then push, so
   both of the new ST(0) and ST(1) are theirs; FYL2X, FYL2XP1 and FPATAN
   write ST(1) and pop; FRSTOR writes every register, FNSAVE reads every
   one; FLDENV, FLDCW, FNCLEX, FNINIT, FFREE and FNSTSW write no register
   value. */
static void
x87_ops_rw(int op, int modrm, int *rd, int *wr, int *xch, uint32_t *fr, uint32_t *fw, uint32_t *gw)
{
    const int reg = (modrm >> 3) & 7;
    const int rm  = modrm & 7;
    const int i   = 1 << rm;

    *rd = *wr = 0;
    *xch      = -1;
    *fr = *fw = *gw = 0;
    if (modrm < 0xc0) {
        switch (op) {
            case 0xd8: /* arithmetic and compares with a memory operand */
            case 0xda:
            case 0xdc:
            case 0xde:
                *rd = 1;
                *wr = ((reg == 2) || (reg == 3)) ? 0 : 1;
                break;
            case 0xd9:
                if (reg == 0)
                    *wr = 1;
                else if ((reg == 2) || (reg == 3))
                    *rd = 1;
                break;
            case 0xdd:
                if (reg == 4) /* FRSTOR */
                    *wr = 0xff;
                else if (reg == 6) /* FNSAVE */
                    *rd = 0xff;
                else if (reg == 0)
                    *wr = 1;
                else if (reg < 4)
                    *rd = 1;
                break;
            default: /* 0xdb, 0xdf */
                if ((reg == 0) || (reg == 5) || ((op == 0xdf) && (reg == 4)))
                    *wr = 1;
                else if ((reg < 4) || (reg == 7) || ((op == 0xdf) && (reg == 6)))
                    *rd = 1;
                break;
        }
        return;
    }
    switch (op) {
        case 0xd8:
            *rd = 1 | i;
            *wr = ((reg == 2) || (reg == 3)) ? 0 : 1;
            break;
        case 0xdc:
            *rd = 1 | i;
            *wr = ((reg == 2) || (reg == 3)) ? 0 : i;
            break;
        case 0xde:
            *rd = 1 | i;
            if ((reg == 3) && (rm == 1)) /* FCOMPP */
                *rd = 3;
            else if ((reg != 2) && (reg != 3) && rm) /* the op's ST(i) is ST(i - 1) after the pop */
                *wr = i >> 1;
            break;
        case 0xd9:
            if (reg == 0) { /* FLD ST(i) */
                *rd = i;
                *wr = 1;
            } else if (reg == 1) /* FXCH */
                *xch = rm;
            else if (reg == 3) { /* FSTP1 */
                *rd = 1;
                *wr = i >> 1;
            } else if (reg == 4) { /* FCHS, FABS, FTST, FXAM */
                *rd = 1;
                *wr = (rm < 2) ? 1 : 0;
            } else if (reg == 5) /* FLD1 .. FLDZ */
                *wr = (rm < 7) ? 1 : 0;
            else if (reg >= 6) {
                switch (modrm) {
                    case 0xf1: /* FYL2X, FPATAN, FYL2XP1 */
                    case 0xf3:
                    case 0xf9:
                        *rd = 3;
                        *wr = 1;
                        break;
                    case 0xf2: /* FPTAN, FXTRACT, FSINCOS */
                    case 0xf4:
                    case 0xfb:
                        *rd = 1;
                        *wr = 3;
                        break;
                    case 0xf5: /* FPREM1, FPREM, FSCALE */
                    case 0xf8:
                    case 0xfd:
                        *rd = 3;
                        *wr = 1;
                        break;
                    case 0xf6: /* FDECSTP, FINCSTP */
                    case 0xf7:
                        break;
                    default: /* F2XM1, FSQRT, FRNDINT, FSIN, FCOS */
                        *rd = *wr = 1;
                        break;
                }
            }
            break;
        case 0xda:
            if (modrm == 0xe9) /* FUCOMPP */
                *rd = 3;
            else if (reg < 4) { /* FCMOVB, FCMOVE, FCMOVBE, FCMOVU */
                static const uint32_t f[4] = { RW_CF, RW_ZF, RW_CF | RW_ZF, RW_PF };

                *rd = 1 | i;
                *wr = 1;
                *fr = f[reg];
            }
            break;
        case 0xdb:
            if (reg < 4) { /* FCMOVNcc */
                static const uint32_t f[4] = { RW_CF, RW_ZF, RW_CF | RW_ZF, RW_PF };

                *rd = 1 | i;
                *wr = 1;
                *fr = f[reg];
            } else if ((reg == 5) || (reg == 6)) { /* FUCOMI, FCOMI */
                *rd = 1 | i;
                *fw = RW_ZF | RW_PF | RW_CF;
            }
            break;
        case 0xdd:
            if (reg == 1) /* FXCH4 */
                *xch = rm;
            else if (reg == 2) { /* FST ST(i) */
                *rd = 1;
                *wr = i;
            } else if (reg == 3) { /* FSTP ST(i) */
                *rd = 1;
                *wr = i >> 1;
            } else if ((reg == 4) || (reg == 5)) /* FUCOM, FUCOMP */
                *rd = 1 | i;
            break;
        default: /* 0xdf */
            if (reg == 1) /* FXCH7 */
                *xch = rm;
            else if ((reg == 2) || (reg == 3)) { /* FSTP8, FSTP9 */
                *rd = 1;
                *wr = i >> 1;
            } else if (modrm == 0xe0) /* FNSTSW AX */
                *gw = RW_EAX;
            else if ((reg == 5) || (reg == 6)) { /* FUCOMIP, FCOMIP */
                *rd = 1 | i;
                *fw = RW_ZF | RW_PF | RW_CF;
            }
            break;
    }
}

/* The registers an instruction at a reads and writes (rw_t), x87
   registers by physical index from s's TOP, which the instruction moves
   (x87_effect()). MMX registers by the MMX tables' forms: a MOVD or MOVQ
   load writes its ModR/M reg operand and reads its r/m one, a store the
   other way, a shift by an immediate reads and writes its r/m operand,
   3DNow!'s PI2FW, PI2FD, PF2IW, PF2ID, PFRCP, PFRSQRT and PSWAPD read only
   their r/m operand, and every other MMX op reads both and writes its reg
   operand. */
static void
insn_rw(const walk_t *w, uint32_t a, uint8_t suffix, x87_sim_t *s, rw_t *out)
{
    const int     x87   = ((w->last_prefix & 0xf8) == 0xd8);
    const uint8_t modrm = x87 ? w->opcode : (w->fetchdat & 0xff);
    const int     mod3  = ((modrm & 0xc0) == 0xc0);
    const int     reg   = (modrm >> 3) & 7;
    const int     rm    = modrm & 7;
    uint32_t      r     = 0;
    uint32_t      o     = 0;

    out->xch = -1;
    if ((w->last_prefix == 0x0f) && mmx_op(w->opcode)) {
        const uint8_t op = w->opcode;

        if (!mod3)
            r |= addr_regs(w->fetchdat, !!(w->op_32 & 0x200));
        if ((op == 0x6e) || (op == 0x6f)) {
            o |= 0x100 << reg;
            if (mod3)
                r |= ((op == 0x6e) ? 1 : 0x100) << rm;
        } else if ((op == 0x7e) || (op == 0x7f)) {
            r |= 0x100 << reg;
            if (mod3)
                o |= ((op == 0x7e) ? 1 : 0x100) << rm;
        } else if ((op >= 0x71) && (op <= 0x73)) {
            r |= 0x100 << rm;
            o |= 0x100 << rm;
        } else {
            const int one = (op == 0x0f) && ((suffix == 0x0c) || (suffix == 0x0d) || (suffix == 0x1c) || (suffix == 0x1d) ||
                                             (suffix == 0x96) || (suffix == 0x97) || (suffix == 0xbb));

            o |= 0x100 << reg;
            if (!one)
                r |= 0x100 << reg;
            if (mod3)
                r |= 0x100 << rm;
        }
    } else if (x87) {
        int      rd;
        int      wr;
        int      xch;
        uint32_t fr;
        uint32_t fw;
        uint32_t gw;

        x87_ops_rw(w->last_prefix, modrm, &rd, &wr, &xch, &fr, &fw, &gw);
        if (!mod3)
            r |= addr_regs(w->fetchdat, !!(w->op_32 & 0x200));
        for (int i = 0; i < 8; i++) {
            if (rd & (1 << i))
                r |= RW_ST(s->top + i);
        }
        if (xch >= 0)
            out->xch = (s->top + xch) & 7;
        x87_effect(s, w->last_prefix, modrm);
        for (int i = 0; i < 8; i++) {
            if (wr & (1 << i))
                o |= RW_ST(s->top + i);
        }
        r |= fr;
        o |= fw | gw;
    } else {
        int_rw(w, a, out);
        return;
    }
    out->rd = r;
    out->wr = o;
}

/* How the copies of a unit read each other's values, the x87 stack
   followed from an empty start: chained 1 when the second copy reads a
   value the first one made (a dependent chain), 0 when it reads none (the
   copies are independent); mindist, over ncopies copies, the fewest
   value-making instructions (FXCH makes none) from an instruction that
   makes a value to one that reads it, counting the reader, 0 when no
   instruction reads another's value. */
typedef struct deps_t {
    int chained;
    int mindist;
} deps_t;

static void
unit_deps(const ins_t *unit, int nunit, int ncopies, deps_t *d)
{
    int       maker[32];
    int       mcopy[32];
    int       k = 0;
    x87_sim_t s;
    walk_t    w;

    memset(&s, 0, sizeof(s));
    memset(d, 0, sizeof(*d));
    for (int b = 0; b < 32; b++)
        maker[b] = -1;
    for (int copy = 0; copy < ncopies; copy++) {
        for (int i = 0; i < nunit; i++) {
            place(unit[i].b, unit[i].len);
            for (uint32_t a = HARNESS_CODE, end = HARNESS_CODE + unit[i].len; a < end;) {
                rw_t rw;

                walk(a, 0, &w);
                insn_rw(&w, a, unit[i].b[unit[i].len - 1], &s, &rw);
                if (rw.xch >= 0) {
                    const int p = 16 + (s.top & 7);
                    const int q = 16 + rw.xch;
                    const int m = maker[p];
                    const int c = mcopy[p];

                    maker[p] = maker[q];
                    mcopy[p] = mcopy[q];
                    maker[q] = m;
                    mcopy[q] = c;
                }
                for (int b = 0; b < 32; b++) {
                    if (!(rw.rd & (1u << b)) || (maker[b] < 0))
                        continue;
                    if ((copy == 1) && (mcopy[b] == 0))
                        d->chained = 1;
                    if (!d->mindist || (k + 1 - maker[b] < d->mindist))
                        d->mindist = k + 1 - maker[b];
                }
                if (rw.wr) {
                    k++;
                    for (int b = 0; b < 32; b++) {
                        if (rw.wr & (1u << b)) {
                            maker[b] = k;
                            mcopy[b] = copy;
                        }
                    }
                }
                a = insn_end(&w, end);
            }
            unplace();
        }
    }
}

static int
unit_chains(const ins_t *unit, int nunit)
{
    deps_t d;

    unit_deps(unit, nunit, 2, &d);
    return d.chained;
}

/* A latency row's instance whose copies do not read each other, made into
   one that does where the same instruction allows it: a register form
   that reads its r/m register reads its reg one, the destination, instead
   (PI2FD MM0,MM1 -> PI2FD MM0,MM0), and a [reg] memory form takes its
   address from its destination when that is EAX (which the run points at
   a dword holding its own address) or the instruction is LEA, which reads
   no memory (MOV EAX,[EBX] -> MOV EAX,[EAX]). Returns 0 with the instance
   rewritten, -1 where neither gives a chain. */
static int
rebuild_chain(ins_t *in)
{
    ins_t  t = *in;
    walk_t w;
    int    at;
    int    mod;
    int    reg;
    int    rm;

    place(t.b, t.len);
    walk(HARNESS_CODE, 0, &w);
    unplace();
    at = (int) (w.op_pc - HARNESS_CODE);
    if (((w.last_prefix & 0xf8) == 0xd8) || (at >= t.len))
        return -1;
    mod = t.b[at] >> 6;
    reg = (t.b[at] >> 3) & 7;
    rm  = t.b[at] & 7;
    if ((mod == 3) && (rm != reg))
        t.b[at] = (t.b[at] & 0xf8) | reg;
    else if ((mod == 0) && (rm != 4) && (rm != 5) && (rm != reg) && (reg != 4) && (reg != 5) &&
             (!reg || (!w.last_prefix && (w.opcode == 0x8d))))
        t.b[at] = (t.b[at] & 0xf8) | reg;
    else
        return -1;
    if (!unit_chains(&t, 1))
        return -1;
    *in = t;
    return 0;
}

static int x87_decode(const ins_t *in, int *op, int *modrm, int *at, int *pfx);
static int x87_setup(const ins_t *unit, int nunit, int copies, const char *cond, meter_x87_t *x, meter_seed_t *sd);

/* 1 when a unit's copies, as many as the x87 stack holds up to 6 and at
   least 4 (a window's LO 2 and two periods), read no value fewer than need
   value-making instructions after it was made. */
static int
stream_ok(const ins_t *unit, int nunit, int need)
{
    meter_x87_t  x;
    meter_seed_t sd;
    deps_t       d;
    int          c = 6;

    while ((c >= 4) && (x87_setup(unit, nunit, c, "", &x, &sd) < 0))
        c--;
    if (c < 4)
        return 0;
    unit_deps(unit, nunit, c, &d);
    return !d.mindist || (d.mindist >= need);
}

/* A Pentium book's x87 throughput row (the /tput half of "a/b") whose
   copies read each other, timed on a stream whose copies read a value no
   sooner than need value-making instructions after it was made, need = a
   / b rounded up: by then it is ready (latency a, a copy every b clocks,
   F.3.2). The row's instance as it is where it already meets that (a = b
   needs nothing); else the same instruction on other registers, a form
   whose r/m field names ST(i) taking three of them in turn (FADD ST(1),ST;
   FADD ST(2),ST; FADD ST(3),ST); else, for an instruction the book pairs
   with FXCH (fx), the instruction and then an FXCH with a register it does
   not use, one register (FADDP ST(1),ST; FXCH ST(3)) or three in turn, so
   that ST(0) comes from four registers: the books' way around the stack
   top (Intel Architecture Optimization Manual 1997, 5.1.2: the FXCH pairs
   with the instruction ahead of it and costs no cycle; Pentium Vol. 3
   F.3.3 note 3). An instruction the book does not pair with FXCH cannot
   take one for free: no stream. Returns the unit's instance count, out
   filled, *nx the row's instructions in it; 0 for none. */
static int
x87_stream(const ins_t *in, int need, int fx, ins_t *out, int *nx)
{
    int      op;
    int      modrm;
    int      at;
    int      pfx;
    int      reg;
    int      rd;
    int      wr;
    int      xch;
    uint32_t fr;
    uint32_t fw;
    uint32_t gw;
    int      n = 0;

    if (x87_decode(in, &op, &modrm, &at, &pfx))
        return 0;
    reg = (modrm >> 3) & 7;
    out[0] = *in;
    if (stream_ok(out, 1, need)) {
        *nx = 1;
        return 1;
    }
    if ((modrm >= 0xc0) && (modrm & 7) &&
        ((op == 0xd8) || (op == 0xdc) || ((op == 0xde) && (modrm != 0xd9)) || ((op == 0xdd) && (reg <= 5)) ||
         ((op == 0xd9) && (reg <= 1)) || ((op == 0xda) && (reg <= 3)) || ((op == 0xdb) && ((reg <= 3) || (reg == 5) || (reg == 6))) ||
         ((op == 0xdf) && ((reg <= 3) || (reg == 5) || (reg == 6))))) {
        for (int k = 0; k < 3; k++) {
            out[k]       = *in;
            out[k].b[at] = (modrm & 0xf8) | (1 + (((modrm & 7) - 1 + k) % 7));
        }
        if (stream_ok(out, 3, need)) {
            *nx = 3;
            return 3;
        }
    }
    if (!fx)
        return 0;
    x87_ops_rw(op, modrm, &rd, &wr, &xch, &fr, &fw, &gw);
    for (int j = 1; j < 8; j++) {
        if ((rd | wr) & (1 << j))
            continue;
        out[0]      = *in;
        out[1].b[0] = 0xd9;
        out[1].b[1] = 0xc8 | j;
        out[1].len  = 2;
        if (stream_ok(out, 2, need)) {
            *nx = 1;
            return 2;
        }
    }
    for (int j = 1; (j < 8) && (n < 6); j++) {
        if ((rd | wr) & (1 << j))
            continue;
        out[n++]     = *in;
        out[n].b[0]  = 0xd9;
        out[n].b[1]  = 0xc8 | j;
        out[n++].len = 2;
    }
    if ((n < 6) || !stream_ok(out, 6, need))
        return 0;
    *nx = 3;
    return 6;
}

/* The value of key in a key=value;... list, copied into v. */
static int
kv_get(const char *list, const char *key, char *v, size_t n)
{
    size_t kl = strlen(key);

    for (const char *p = list; p && *p;) {
        const char *e = strchr(p, ';');
        size_t      l = e ? (size_t) (e - p) : strlen(p);

        if ((l > kl) && !strncmp(p, key, kl) && (p[kl] == '=')) {
            size_t vl = l - kl - 1;

            if (vl >= n)
                vl = n - 1;
            memcpy(v, p + kl + 1, vl);
            v[vl] = 0;
            return 1;
        }
        p = e ? e + 1 : NULL;
    }
    return 0;
}

/* ---- the book file --------------------------------------------------- */

static char *
read_all(const char *name)
{
    FILE *f = fopen(name, "rb");
    long  len;
    char *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(len + 1);
    if (buf && (fread(buf, 1, len, f) != (size_t) len)) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[len] = 0;
    fclose(f);
    return buf;
}

static char empty[] = "";

static int
split_tabs(char *line, char **cells, int max)
{
    int n = 0;

    cells[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '\t') {
            *p = 0;
            if (n < max)
                cells[n++] = p + 1;
        }
    }
    return n;
}

static int
row_cmp(const void *a, const void *b)
{
    const row_t *x = a;
    const row_t *y = b;
    int          c = strcmp(x->id, y->id);

    if (!c)
        c = strcmp(x->f[C_PARTS], y->f[C_PARTS]);
    return c ? c : (x->line - y->line);
}

static int
token_match(const char *parts, const char *family, const char *family_name, const char *cpu_name, const char *aliases)
{
    char buf[1024];

    snprintf(buf, sizeof(buf), "%s", parts);
    for (char *t = strtok(buf, ", "); t; t = strtok(NULL, ", ")) {
        if (!strcmp(t, "*") || !strcasecmp(t, "all") || !strcasecmp(t, family) || !strcasecmp(t, family_name) ||
            !strcasecmp(t, cpu_name))
            return 1;
        if (aliases) {
            const char *p = aliases;

            while (*p) {
                const char *e = strchr(p, ',');
                size_t      l = e ? (size_t) (e - p) : strlen(p);

                if ((strlen(t) == l) && !strncasecmp(t, p, l))
                    return 1;
                p = e ? e + 1 : p + l;
            }
        }
    }
    return 0;
}

/* ---- scoring --------------------------------------------------------- */

enum {
    V_PASS,
    V_FAIL,
    V_RANGE_IN,
    V_NO_BOOK,
    V_SPLIT,
    V_SIL_PASS,
    V_SIL_FAIL,
    V_SIL_RANGE_IN,
    V_FORMULA,
    V_PROSE,
    V_NO_MODEL,
    V_RIDER,
    V_NORUN,
    V_BRANCH,
    V_ERROR,
    V_NOCHAIN,
    V_NOSTREAM,
    V_OFFMODEL,
    V_NR
};

/* FORMULA: a book figure that is not a number or a range (a formula in a
   count the run does not vary, "INT+51"); PROSE: a struct row whose book
   value is words; NO-MODEL: a struct key the model's table does not hold,
   or a cache or TLB miss penalty; RIDER: a part with no book, measured on
   another part's row; NORUN: verdict_i of a row with no end-to-end run;
   BRANCH: verdict_i of a conditional branch whose run takes the path the
   flags it finds pick, which the row's model figure need not be; NOCHAIN:
   a latency row whose copies cannot be made to read each other; NOSTREAM:
   a throughput row whose copies read each other; NOT-MEASURED: a row
   whose charge the emulator's compiled code makes is neither measured by
   the end-to-end run nor reproduced exactly (off_model()). None of them is
   scored; the delta of NOCHAIN, NOSTREAM and NOT-MEASURED names why
   ("unscored:<reason>"). */
static const char *v_names[V_NR] = { "PASS",    "FAIL",     "RANGE-IN", "NO-BOOK", "SPLIT", "SIL-PASS",
                                     "SIL-FAIL", "SIL-RANGE-IN", "FORMULA", "PROSE", "NO-MODEL", "RIDER",
                                     "NORUN",   "BRANCH",   "ERROR",    "NOCHAIN", "NOSTREAM", "NOT-MEASURED" };
static int         v_count[V_NR];
static int         vi_count[V_NR];
/* A rep row's verdict, scored on the compiled run's element charge (a
   backend's, not the model's): counted apart, as C-<verdict>. */
static int         vc_count[V_NR];
static int         gap_count;
static int         model_ctx_count; /* wire "=ctx" */
static int         norun_count;
static int         wire_run_count;
static int         frame_same_count; /* single rows: both frames give the same charge */
static int         frame_diff_count;

static int
scored(int v)
{
    return (v == V_PASS) || (v == V_FAIL) || (v == V_RANGE_IN) || (v == V_SIL_PASS) || (v == V_SIL_FAIL) || (v == V_SIL_RANGE_IN);
}

static int
is_struct_row(const row_t *r)
{
    const char *m = r->f[C_MEANING];

    return !strcmp(m, "uops") || !strcmp(m, "decoder") || !strcmp(m, "pairing") || !strcmp(m, "ports") ||
           !strncmp(r->f[C_KIND], "struct", 6);
}

/* Rows that state a figure for an instruction; a book file also carries
   its assumptions, footnotes and legends, which are not measured. */
static int
is_measured_meaning(const char *m)
{
    return !strcmp(m, "clocks") || !strcmp(m, "throughput") || !strcmp(m, "latency") || !strcmp(m, "uops") ||
           !strcmp(m, "decoder") || !strcmp(m, "pairing") || !strcmp(m, "ports");
}

/* Conditions the harness's flat 32-bit ring 0 protected mode meets, and a
   branch's taken or not taken, which the run's start state sets
   (path_state()), and an x87 precision control (x87_setup()). */
static int
cond_runs(const char *cond)
{
    return !cond[0] || !strcmp(cond, "-") || !strcmp(cond, "pm") || !strcmp(cond, "hit") || !strcmp(cond, "taken") ||
           !strcmp(cond, "nottaken") || !strcmp(cond, "pc24") || !strcmp(cond, "pc53") || !strcmp(cond, "pc64");
}

/* The per-count term of a book formula "a+b<count>" ("7+7c", "5+4n"; "13+c"
   is 1 per count), or NULL. */
static const char *
formula_slope(const char *book, char *out, size_t n)
{
    const char *plus = strchr(book, '+');
    const char *e;
    int64_t     v;

    if (!plus || !parse_x100(book, plus, &v))
        return NULL;
    e = plus + 1;
    while (*e && (isdigit((unsigned char) *e) || (*e == '.')))
        e++;
    if (!isalpha((unsigned char) *e) || e[1])
        return NULL;
    if (e == plus + 1)
        snprintf(out, n, "1");
    else
        snprintf(out, n, "%.*s", (int) (e - plus - 1), plus + 1);
    return out;
}

static void
score_value(const char *book, int64_t model, int sil, int *verdict, char *delta, char *dir, size_t n)
{
    const char *dash = strchr(book + (book[0] == '-' ? 1 : 0), '-');
    int64_t     lo;
    int64_t     hi;

    snprintf(delta, n, "-");
    snprintf(dir, n, "-");
    if (dash && parse_x100(book, dash, &lo) && parse_x100(dash + 1, book + strlen(book), &hi)) {
        if ((model >= lo) && (model <= hi)) {
            *verdict = sil ? V_SIL_RANGE_IN : V_RANGE_IN;
            snprintf(dir, n, "=");
        } else {
            int64_t d = (model < lo) ? model - lo : model - hi;

            *verdict = sil ? V_SIL_FAIL : V_FAIL;
            fmt_x100(delta, n, d);
            snprintf(dir, n, (d > 0) ? "over" : "under");
        }
        return;
    }
    if (!parse_x100(book, book + strlen(book), &lo)) {
        *verdict = V_FORMULA;
        return;
    }
    fmt_x100(delta, n, model - lo);
    if (model == lo) {
        *verdict = sil ? V_SIL_PASS : V_PASS;
        snprintf(dir, n, "=");
    } else {
        *verdict = sil ? V_SIL_FAIL : V_FAIL;
        snprintf(dir, n, (model > lo) ? "over" : "under");
    }
}

/* A comma list as a sorted set, for keys whose order says nothing (ports). */
static void
sort_list(char *s)
{
    char  *tok[32];
    int    nt = 0;
    char   buf[256];
    size_t len = 0;

    snprintf(buf, sizeof(buf), "%s", s);
    for (char *t = strtok(buf, ","); t && (nt < 32); t = strtok(NULL, ","))
        tok[nt++] = t;
    for (int i = 1; i < nt; i++) {
        for (int j = i; (j > 0) && (strcmp(tok[j - 1], tok[j]) > 0); j--) {
            char *x    = tok[j];
            tok[j]     = tok[j - 1];
            tok[j - 1] = x;
        }
    }
    s[0] = 0;
    for (int i = 0; i < nt; i++) {
        if (!i || strcmp(tok[i], tok[i - 1]))
            len += sprintf(s + len, "%s%s", len ? "," : "", tok[i]);
    }
}

/* The book's key=value pairs against the model's. A bare value is the
   value of the row's meaning (pairing: the model's pair key). */
static void
score_struct(const row_t *r, const char *model, int sil, int *verdict, char *delta, size_t n)
{
    char buf[512];
    char mv[128];
    char bv[128];
    int  bad    = 0;
    int  absent = 0;

    const char *book = r->f[C_BOOK];

    /* The Cyrix books' exclusive instructions, which "cannot be paired
       with instructions in the Y pipeline" (6x86 Data Book p. 1-4): the
       model's NP, an instruction that pairs with nothing. */
    if (!strcmp(r->f[C_MEANING], "pairing") && !strncasecmp(book, "Exclusive Instructions cannot be paired", 39))
        book = "NP";
    delta[0] = 0;
    if (!strchr(book, '='))
        snprintf(buf, sizeof(buf), "%s=%s", !strcmp(r->f[C_MEANING], "pairing") ? "pair" : r->f[C_MEANING], book);
    else
        snprintf(buf, sizeof(buf), "%s", book);
    if (strchr(buf, ' ')) {
        *verdict = V_PROSE;
        snprintf(delta, n, "-");
        return;
    }
    for (char *t = buf, *next; t && *t; t = next) {
        char *eq;

        next = strchr(t, ';');
        if (next)
            *next++ = 0;
        eq = strchr(t, '=');
        if (!eq) {
            *verdict = V_ERROR;
            snprintf(delta, n, "book?");
            return;
        }
        *eq = 0;
        snprintf(bv, sizeof(bv), "%s", eq + 1);
        if (!kv_get(model, t, mv, sizeof(mv))) {
            meter_putf(delta, n, "%s%s:absent", (bad || absent) ? "," : "", t);
            absent = 1;
            continue;
        }
        if (!strcmp(t, "ports")) {
            sort_list(mv);
            sort_list(bv);
        }
        /* The Pentium books' pairing labels against the model's classes:
           PU pairs in the U pipe only (U), PV in the V pipe only (V), NP
           not at all (NP, or an x87 entry the model marks FPU, which lacks
           the U pipe bit and pairs with nothing, FXCH included). FX (pairs
           with FXCH) is the model's FX alone. */
        if (!strcmp(t, "pair")) {
            if (!strcasecmp(bv, "PU"))
                snprintf(bv, sizeof(bv), "U");
            else if (!strcasecmp(bv, "PV"))
                snprintf(bv, sizeof(bv), "V");
            else if (!strcasecmp(bv, "NP") && !strcasecmp(mv, "FPU"))
                snprintf(bv, sizeof(bv), "FPU");
        }
        /* A book's "complex" uop count is a microcode-ROM sequence, more
           than 4 uops. */
        if (!strcmp(t, "uops") && !strcasecmp(bv, "complex")) {
            if (atoi(mv) <= 4) {
                meter_putf(delta, n, "%s%s:%s<=4", (bad || absent) ? "," : "", t, mv);
                bad = 1;
            }
            continue;
        }
        if (strcasecmp(mv, bv)) {
            meter_putf(delta, n, "%s%s:%s!=%s", (bad || absent) ? "," : "", t, mv, bv);
            bad = 1;
        }
    }
    if (bad && kv_get(model, "prefix_delay", mv, sizeof(mv)) && kv_get(model, "pair_table", bv, sizeof(bv)))
        meter_putf(delta, n, " (the prefix decode delay %s, unshadowed, keeps it from pairing: "
                             "codegen_timing_pentium_prefix(), not the table entry, %s)", mv, bv);
    if (bad)
        *verdict = sil ? V_SIL_FAIL : V_FAIL;
    else if (absent)
        *verdict = V_NO_MODEL;
    else {
        *verdict = sil ? V_SIL_PASS : V_PASS;
        snprintf(delta, n, "-");
    }
}

/* ---- measuring a unit ------------------------------------------------ */

/* LO and HI copies with LO 2 (a warm-up neighbor never sits next to a
   measured copy in one and not the other), HI within the block, and the
   difference a multiple of 6 instructions where there is room. */
static void
copies_for(int ninsn, int *lo, int *hi)
{
    int maxc = BLOCK_FIT / ninsn;

    *lo = 2;
    for (int h = maxc; h > 2; h--) {
        if (!(((h - 2) * ninsn) % 6)) {
            *hi = h;
            return;
        }
    }
    *hi = (maxc > 2) ? maxc : 3;
}

static int build(const ins_t *const *pre, int npre, const ins_t *unit, int nunit, int copies, int ctx, const ins_t **stream);

/* LO and HI copies of a unit of ninsn instructions, from totals over 0 ..
   maxc copies (maxc = BLOCK_FIT / ninsn), one series for each stream the
   row's figures difference (t[0 .. ns - 1]): the model's isolated block,
   and the frame's ctx stream, for the row's instruction and, for a prefix
   row, its carrier too. LO the first count from which every series grows
   by the same amount every p copies to maxc, at least 2 and with two
   periods to spare (the smallest such LO over p 1 to 6, then the smallest
   p): past the start, where a pipeline that begins empty still charges
   less (the Pentium FPU runs 3 FDIVs before the fourth waits on the
   first), and past the frame's edges, where the copies next to the
   warm-up or the epilogue pack with them (a P6 decode group). HI the
   largest count with HI - LO a multiple of p and, where there is room, of
   6 instructions. Series that never settle together keep copies_for(),
   period 1. */
static int x87_setup(const ins_t *unit, int nunit, int copies, const char *cond, meter_x87_t *x, meter_seed_t *sd);

static void
choose_window(int64_t (*t)[BLOCK_FIT + 1], int ns, int maxc, int ninsn, int *lo, int *hi, int *per)
{
    int bl = -1;
    int bp = 1;

    copies_for(ninsn, lo, hi);
    *per = 1;
    for (int p = 1; (p <= 6) && ns; p++) {
        for (int l = 2; (l + 2 * p <= maxc) && ((bl < 0) || (l < bl)); l++) {
            int same = 1;

            for (int s = 0; same && (s < ns); s++) {
                for (int n = l; same && (n + p <= maxc); n++)
                    same = ((t[s][n + p] - t[s][n]) == (t[s][l + p] - t[s][l]));
            }
            if (same) {
                bl = l;
                bp = p;
                break;
            }
        }
    }
    if (bl < 0)
        return;
    *lo  = bl;
    *per = bp;
    *hi  = -1;
    for (int h = maxc; h > bl; h--) {
        if ((h - bl) % bp)
            continue;
        if (*hi < 0)
            *hi = h;
        if (!(((h - bl) * ninsn) % 6)) {
            *hi = h;
            break;
        }
    }
}

/* The totals of 0 .. maxc copies of a unit: the model's (ctx 0) or the
   frame's (ctx 1). -1 when a drive fails. */
static int
unit_totals(const ins_t *unit, int nunit, int maxc, int taken, int ctx, int64_t *t)
{
    static const ins_t *stream[STREAM_MAX * UNIT_MAX];

    for (int c = 0; c <= maxc; c++) {
        if (drive(stream, build(NULL, 0, unit, nunit, c, ctx, stream), taken, ctx, &t[c]))
            return -1;
    }
    return 0;
}

/* HI comes down by p while x87 instructions' copies need more than the 8
   registers (x87_setup()) and LO stays below it. */
static void
x87_fit(const ins_t *unit, int nunit, int p, int lo, int *hi)
{
    meter_x87_t  x;
    meter_seed_t sd;

    while ((*hi - p > lo) && (x87_setup(unit, nunit, *hi, "", &x, &sd) < 0))
        *hi -= p;
}

/* Totals (model_t, ctx_t, wc_t, wi_t: hi minus lo copies) and the same per
   copy, x100: the model's over mdiv, the rest over div. */
typedef struct meas_t {
    int     has_model;
    int64_t model;
    int64_t model_t;
    int     mdiv;
    int     has_ctx;
    int64_t ctx;
    int64_t ctx_t;
    int     has_ctx_alt; /* a conditional branch's other path */
    int64_t ctx_alt;
    int     has_wire;
    int64_t wc;
    int64_t wi;
    int64_t wc_t;
    int64_t wi_t;
    int     div;
    int     cond_branch;
    char    why[24];
    char    mwhy[16]; /* why there is no model figure ("fatal" when empty) */
    int     alone;    /* single_stream(): the timed instance ended its block in ctx's run */
} meas_t;

static int
build(const ins_t *const *pre, int npre, const ins_t *unit, int nunit, int copies, int ctx, const ins_t **stream)
{
    int n = 0;

    if (ctx) {
        for (int i = 0; i < nframe_pre; i++)
            stream[n++] = &frame_pre[i];
        for (int i = 0; i < NWARM; i++)
            stream[n++] = &warm[i];
    }
    for (int c = 0; c < copies; c++) {
        for (int i = 0; i < npre; i++)
            stream[n++] = pre[i];
        for (int i = 0; i < nunit; i++)
            stream[n++] = &unit[i];
    }
    if (ctx) {
        for (int i = 0; i < nframe_time; i++)
            stream[n++] = &frame_epi[i];
    }
    return n;
}

/* A single row's instance timed alone, as the model's isolated block times
   it: the last instruction of its block, which the recompiler ends at its
   instruction limit, so nothing after it shares its block; a pairable
   instruction held for a partner (Pentium U, 6x86 X/Y) is charged at that
   block end, its prefix delay included, as in the isolated block. Copies
   of the instance (single_warm()) come first in both runs and leave the
   state the copies ahead of a timed one leave for the other rows (a page's
   first access, the stack and EBP a LEAVE moves), with no separator next
   to them. Separators fill the rest of the block (ahead: measure() picks
   the count that puts the instance on the limit when a copy ends a block,
   an MMX op entering MMX mode; drive() reports in meas_t alone whether it
   ended its block). The two nearest it are FWAITs: not pairable on the
   Pentium or the 6x86, writing no register and reading no address
   register, so the instance pairs with neither and no AGI reaches it (the
   6x86 looks two instructions back), and on the Pentium 1 clock, which
   leaves no decode time to shadow its prefixes. On the WinChip 2 they are
   NOPs: its FWAIT takes 4 clocks and would shadow the instance's prefix and
   escape decode clocks; its NOP takes 1, writes no register and does not
   pair (only MMX instructions pair there). After the instance, two
   separators and one per instruction of the instance start the next block,
   ahead of the epilogue. The run without the instance has one separator
   per instruction of the instance in its place, so both runs end their
   first block at the same instruction and their second block holds the
   same epilogue; a separator's charge does not depend on what is before it
   (the in-order models), so the extra ones charge the same in either
   block. Frame 0 fills the block with FWAITs, frame 1 with CLDs up to the
   two nearest separators, and on the 486 model (no pairing, an AGI one
   instruction back) CLDs next to and after the instance too: the two
   frames' figures are counted (COUNT FRAME-SAME, FRAME-DIFF), and on the
   in-order models a figure other than the isolated block's (model) is not
   measured (meter_row()). The out-of-order models (P6, K6) charge an
   instance the step of an in-order completion time over what is in flight
   around it: no separator isolates it, only an empty block: their single
   rows are scored from it where no run was made, else not measured (the
   copies' figures shown). Returns the stream length, or -1 for fewer than
   two separators ahead. */
static const ins_t sep_fwait = { { 0x9b }, 1 };
static const ins_t sep_cld   = { { 0xfc }, 1 };
static const ins_t sep_nop   = { { 0x90 }, 1 };

#define SEP_AFTER   2

static int single_at; /* the stream index of the timed instance's last instance */

/* The copies ahead: 2, or none for x87 instructions on the models that
   carry an x87 result latency from one instruction to the next (the
   Pentium's, the WinChip 2's), which the separators do not wait out and
   the isolated block does not have; their memory operands are on the
   data page the warm-up fills. */
static int
single_warm(const ins_t *unit, int nunit)
{
    int op;
    int modrm;
    int at;
    int pfx;

    if (strcmp(model_name, "pentium") && strcmp(model_name, "winchip2"))
        return 2;
    for (int i = 0; i < nunit; i++) {
        if (!x87_decode(&unit[i], &op, &modrm, &at, &pfx))
            return 0;
    }
    return 2;
}

/* The separators ahead that fill the first block when no copy ends a
   block early. */
static int
single_ahead_max(const ins_t *unit, int nunit)
{
    return MAX_INSTRUCTION_COUNT - nframe_pre - NWARM - (single_warm(unit, nunit) + 1) * count_insns(unit, nunit);
}

static int
single_stream(const ins_t *unit, int nunit, int ahead, int with, int frame, int ctx, const ins_t **stream)
{
    const ins_t *near = !strcmp(model_name, "winchip2") ? &sep_nop : (frame && !strcmp(model_name, "486")) ? &sep_cld : &sep_fwait;
    const int    ni   = count_insns(unit, nunit);
    int          n    = 0;

    if (ahead < 2)
        return -1;
    if (ctx) {
        for (int i = 0; i < nframe_pre; i++)
            stream[n++] = &frame_pre[i];
    }
    for (int i = 0; i < NWARM; i++)
        stream[n++] = &warm[i];
    for (int c = single_warm(unit, nunit); c > 0; c--) {
        for (int i = 0; i < nunit; i++)
            stream[n++] = &unit[i];
    }
    for (int i = 0; i < ahead; i++)
        stream[n++] = (frame && (i < ahead - 2)) ? &sep_cld : near;
    if (with) {
        for (int i = 0; i < nunit; i++)
            stream[n++] = &unit[i];
        single_at = n - 1;
    } else {
        for (int i = 0; i < ni; i++)
            stream[n++] = near;
    }
    for (int i = 0; i < SEP_AFTER + (with ? ni : 0); i++)
        stream[n++] = near;
    if (ctx) {
        for (int i = 0; i < nframe_time; i++)
            stream[n++] = &frame_epi[i];
    }
    return n;
}

/* ---- the x87 start state of a row's runs ------------------------------ */

/* A row's x87 instructions run on what the book times: operands that are
   valid registers holding normal values, pushes into empty registers, and
   memory operands holding normal values, never the masked stack fault or
   special-operand path. x87_effect() follows the tags over every copy of
   the hi run (the lo run is its prefix); x87_setup() picks the deepest
   start stack the copies keep within 0..8 registers, or none fits: the row
   is not timed (x87depth). */
enum {
    XM_NONE,
    XM_R32,
    XM_R64,
    XM_R80,
    XM_I16,
    XM_I32,
    XM_I64,
    XM_BCD,
    XM_CW,
    XM_ENV,
    XM_SAVE
};

/* ST(0) and ST(k) are read */
static void
xs_need(x87_sim_t *s, int k)
{
    if (!s->valid[s->top] || !s->valid[(s->top + k) & 7])
        s->bad = 1;
}

static void
xs_push(x87_sim_t *s)
{
    s->top = (s->top - 1) & 7;
    if (s->valid[s->top])
        s->bad = 1;
    s->valid[s->top] = 1;
}

static void
xs_pop(x87_sim_t *s)
{
    s->valid[s->top] = 0;
    s->top           = (s->top + 1) & 7;
}

/* The x87 opcode byte and its ModR/M past the prefixes and any FWAIT
   written ahead of it, or -1: not an x87 instruction. pfx: 1 an operand-size
   prefix (16-bit environment images), 2 an address-size prefix (16-bit
   addressing). */
static int
x87_decode(const ins_t *in, int *op, int *modrm, int *at, int *pfx)
{
    int i = 0;

    *pfx = 0;
    while ((i < in->len) && ((in->b[i] == 0x9b) || (in->b[i] == 0x66) || (in->b[i] == 0x67) || (in->b[i] == 0xf0) ||
                             (in->b[i] == 0xf2) || (in->b[i] == 0xf3) || (in->b[i] == 0x26) || (in->b[i] == 0x2e) ||
                             (in->b[i] == 0x36) || (in->b[i] == 0x3e) || (in->b[i] == 0x64) || (in->b[i] == 0x65))) {
        if (in->b[i] == 0x66)
            *pfx |= 1;
        else if (in->b[i] == 0x67)
            *pfx |= 2;
        i++;
    }
    if ((i + 1 >= in->len) || (in->b[i] < 0xd8) || (in->b[i] > 0xdf))
        return -1;
    *op    = in->b[i];
    *modrm = in->b[i + 1];
    *at    = i + 1;
    return 0;
}

/* One x87 instruction on the tags; returns the format of the memory
   operand it reads (XM_NONE for a register form or a store). */
static int
x87_effect(x87_sim_t *s, int op, int modrm)
{
    const int reg = (modrm >> 3) & 7;
    const int rm  = modrm & 7;

    if (modrm < 0xc0) {
        switch (op) {
            case 0xd8:
            case 0xdc:
                xs_need(s, 0);
                if (reg == 3)
                    xs_pop(s);
                return (op == 0xd8) ? XM_R32 : XM_R64;
            case 0xda:
            case 0xde:
                xs_need(s, 0);
                if (reg == 3)
                    xs_pop(s);
                return (op == 0xda) ? XM_I32 : XM_I16;
            case 0xd9:
                switch (reg) {
                    case 0:
                        xs_push(s);
                        return XM_R32;
                    case 2:
                        xs_need(s, 0);
                        return XM_NONE;
                    case 3:
                        xs_need(s, 0);
                        xs_pop(s);
                        return XM_NONE;
                    case 4: /* FLDENV: the image's tag word marks every register valid, TOP 0 */
                        memset(s->valid, 1, sizeof(s->valid));
                        s->top = 0;
                        return XM_ENV;
                    case 5:
                        return XM_CW;
                    default:
                        return XM_NONE;
                }
            case 0xdb:
            case 0xdd:
            case 0xdf:
                switch (reg) {
                    case 0:
                        xs_push(s);
                        return (op == 0xdb) ? XM_I32 : (op == 0xdd) ? XM_R64 : XM_I16;
                    case 1:
                    case 3:
                        xs_need(s, 0);
                        xs_pop(s);
                        return XM_NONE;
                    case 2:
                        xs_need(s, 0);
                        return XM_NONE;
                    case 4:
                        if (op == 0xdd) { /* FRSTOR */
                            memset(s->valid, 1, sizeof(s->valid));
                            s->top = 0;
                            return XM_SAVE;
                        }
                        if (op == 0xdf) { /* FBLD */
                            xs_push(s);
                            return XM_BCD;
                        }
                        return XM_NONE;
                    case 5:
                        if (op == 0xdd)
                            return XM_NONE;
                        xs_push(s);
                        return (op == 0xdb) ? XM_R80 : XM_I64;
                    case 6:
                        if (op == 0xdd) { /* FNSAVE: then as FNINIT */
                            memset(s->valid, 0, sizeof(s->valid));
                            s->top = 0;
                            return XM_NONE;
                        }
                        if (op == 0xdf) { /* FBSTP */
                            xs_need(s, 0);
                            xs_pop(s);
                        }
                        return XM_NONE;
                    default:
                        if (op != 0xdd) { /* FSTP m80, FISTP m64 */
                            xs_need(s, 0);
                            xs_pop(s);
                        }
                        return XM_NONE;
                }
            default:
                return XM_NONE;
        }
    }
    switch (op) {
        case 0xd8:
            xs_need(s, 0);
            xs_need(s, rm);
            if (reg == 3)
                xs_pop(s);
            break;
        case 0xdc:
            xs_need(s, 0);
            xs_need(s, rm);
            if (reg == 3)
                xs_pop(s);
            break;
        case 0xde:
            if ((reg == 3) && (rm != 1))
                break;
            xs_need(s, (reg == 3) ? 1 : rm);
            xs_pop(s);
            if (reg == 3)
                xs_pop(s);
            break;
        case 0xd9:
            if (reg == 0) { /* FLD ST(i) */
                xs_need(s, rm);
                xs_push(s);
            } else if (reg == 1) { /* FXCH */
                xs_need(s, rm);
            } else if (reg == 3) { /* FSTP1 */
                xs_need(s, 0);
                s->valid[(s->top + rm) & 7] = 1;
                xs_pop(s);
            } else if (reg == 4) {
                if (rm <= 1 || rm == 4 || rm == 5)
                    xs_need(s, 0);
            } else if (reg == 5) {
                if (rm <= 6)
                    xs_push(s); /* FLD1 .. FLDZ */
            } else if (reg == 6) {
                switch (rm) {
                    case 0: /* F2XM1 */
                        xs_need(s, 0);
                        break;
                    case 1: /* FYL2X */
                    case 3: /* FPATAN */
                        xs_need(s, 1);
                        xs_pop(s);
                        break;
                    case 2: /* FPTAN */
                    case 4: /* FXTRACT */
                        xs_need(s, 0);
                        xs_push(s);
                        break;
                    case 5: /* FPREM1 */
                        xs_need(s, 1);
                        break;
                    case 6: /* FDECSTP */
                        s->top = (s->top - 1) & 7;
                        break;
                    default: /* FINCSTP */
                        s->top = (s->top + 1) & 7;
                        break;
                }
            } else if (reg == 7) {
                switch (rm) {
                    case 1: /* FYL2XP1 */
                        xs_need(s, 1);
                        xs_pop(s);
                        break;
                    case 0: /* FPREM */
                    case 5: /* FSCALE */
                        xs_need(s, 1);
                        break;
                    case 3: /* FSINCOS */
                        xs_need(s, 0);
                        xs_push(s);
                        break;
                    default:
                        xs_need(s, 0);
                        break;
                }
            }
            break;
        case 0xda:
            if (modrm == 0xe9) { /* FUCOMPP */
                xs_need(s, 1);
                xs_pop(s);
                xs_pop(s);
            } else if (reg < 4) { /* FCMOVcc */
                xs_need(s, 0);
                xs_need(s, rm);
            }
            break;
        case 0xdb:
            if (modrm == 0xe3) { /* FNINIT */
                memset(s->valid, 0, sizeof(s->valid));
                s->top = 0;
            }
            else if ((reg < 4) || (reg == 5) || (reg == 6)) { /* FCMOVNcc, FUCOMI, FCOMI */
                xs_need(s, 0);
                xs_need(s, rm);
            }
            break;
        case 0xdd:
            if (reg == 0) /* FFREE */
                s->valid[(s->top + rm) & 7] = 0;
            else if (reg == 1) /* FXCH */
                xs_need(s, rm);
            else if ((reg == 2) || (reg == 3)) { /* FST, FSTP ST(i) */
                xs_need(s, 0);
                s->valid[(s->top + rm) & 7] = 1;
                if (reg == 3)
                    xs_pop(s);
            } else if ((reg == 4) || (reg == 5)) { /* FUCOM, FUCOMP */
                xs_need(s, rm);
                if (reg == 5)
                    xs_pop(s);
            }
            break;
        default: /* 0xdf */
            if (reg == 0) { /* FFREEP */
                s->valid[(s->top + rm) & 7] = 0;
                xs_pop(s);
            } else if (reg == 1) /* FXCH */
                xs_need(s, rm);
            else if ((reg == 2) || (reg == 3)) { /* FSTP ST(i) */
                xs_need(s, 0);
                s->valid[(s->top + rm) & 7] = 1;
                xs_pop(s);
            } else if ((reg == 5) || (reg == 6)) { /* FUCOMIP, FCOMIP */
                xs_need(s, rm);
                xs_pop(s);
            }
            break;
    }
    return XM_NONE;
}

/* The linear address of a memory operand from the registers the meter's
   case starts with (harness_meter_wire(): EAX, EBX, ESI, EDI into the data
   page, EBP into the stack page; segment bases 0), with an 8-, 16- or
   32-bit displacement; 16-bit addressing under an address-size prefix
   (the offset wraps at 64 KB). 0: an operand the meter does not set (an
   ECX, EDX, ESP or SIB base). */
static uint32_t
x87_mem_addr(const ins_t *in, int at, int addr16)
{
    const uint32_t eax   = HARNESS_DATA + 0x100;
    const uint32_t ebx   = HARNESS_DATA + 0x800;
    const uint32_t ebp   = HARNESS_STACK + 0x800;
    const uint32_t esi   = HARNESS_DATA;
    const uint32_t edi   = HARNESS_DATA + 0x400;
    const int      modrm = in->b[at];
    const int      mod   = modrm >> 6;
    const int      rm    = modrm & 7;
    uint32_t       base;
    int32_t        disp  = 0;
    int            n     = 0;

    if (addr16) {
        static const int dsz[3] = { 0, 1, 2 };

        if ((mod == 0) && (rm == 6))
            return 0; /* [disp16] */
        switch (rm) {
            case 0:
                base = ebx + esi;
                break;
            case 1:
                base = ebx + edi;
                break;
            case 2:
                base = ebp + esi;
                break;
            case 3:
                base = ebp + edi;
                break;
            case 4:
                base = esi;
                break;
            case 5:
                base = edi;
                break;
            case 6:
                base = ebp;
                break;
            default:
                base = ebx;
                break;
        }
        n = dsz[mod];
    } else {
        switch (rm) {
            case 0:
                base = eax;
                break;
            case 3:
                base = ebx;
                break;
            case 5:
                if (mod == 0)
                    return 0; /* [disp32] */
                base = ebp;
                break;
            case 6:
                base = esi;
                break;
            case 7:
                base = edi;
                break;
            default:
                return 0;
        }
        n = (mod == 1) ? 1 : (mod == 2) ? 4 : 0;
    }
    if (at + n >= in->len)
        return 0;
    if (n == 1)
        disp = (int8_t) in->b[at + 1];
    else if (n == 2)
        disp = (int16_t) (in->b[at + 1] | (in->b[at + 2] << 8));
    else if (n == 4)
        disp = (int32_t) (in->b[at + 1] | (in->b[at + 2] << 8) | (in->b[at + 3] << 16) | ((uint32_t) in->b[at + 4] << 24));
    base += (uint32_t) disp;
    return addr16 ? (base & 0xffff) : base;
}

/* A double (normal) as an 80-bit register image, little-endian. */
static void
x87_put80(uint8_t *p, double v)
{
    uint64_t b;
    uint64_t sig;
    int      e;

    memcpy(&b, &v, 8);
    e   = (int) ((b >> 52) & 0x7ff) - 1023 + 16383;
    sig = (1ULL << 63) | ((b & 0xfffffffffffffULL) << 11);
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t) (sig >> (8 * i));
    p[8] = (uint8_t) e;
    p[9] = (uint8_t) (((e >> 8) & 0x7f) | ((b >> 63) ? 0x80 : 0));
}

static void
x87_put(uint8_t *p, uint64_t v, int n)
{
    for (int i = 0; i < n; i++)
        p[i] = (i < 8) ? (uint8_t) (v >> (8 * i)) : 0;
}

/* 1: the unit holds an x87 instruction and x87 is set up for its runs; 0:
   no x87 instruction (the default start state); -1: no start stack keeps
   the copies within the stack. cond pc24, pc53 or pc64 sets the precision
   control. ST(i) is 1.5 + 0.25i, positive, above 1 and distinct (a
   logarithm, a division or a remainder chain keeps normal operands), or
   0.125 + 0.0625i for F2XM1 and FYL2XP1, whose operand range is below 1
   (Pentium Vol. 2 F2XM1, FYL2XP1); a memory operand is 1.5 or the integer 3
   in its format, FLDCW's the row's control word, FLDENV's and FRSTOR's
   images every register valid; sd holds that operand (len 0: none, or an
   address the meter does not set). */
static int
x87_setup(const ins_t *unit, int nunit, int copies, const char *cond, meter_x87_t *x, meter_seed_t *sd)
{
    int      op;
    int      modrm;
    int      at;
    int      pfx;
    int      any   = 0;
    int      small = 0;
    int      fmt   = XM_NONE;
    int      env16 = 0;
    uint32_t addr  = 0;
    int      depth = -1;
    double   val[8];

    for (int i = 0; i < nunit; i++) {
        if (x87_decode(&unit[i], &op, &modrm, &at, &pfx))
            continue;
        any = 1;
        if ((op == 0xd9) && ((modrm == 0xf0) || (modrm == 0xf9)))
            small = 1;
    }
    if (!any)
        return 0;
    for (int d = 8; (d >= 0) && (depth < 0); d--) {
        x87_sim_t s;

        memset(&s, 0, sizeof(s));
        for (int i = 0; i < d; i++)
            s.valid[i] = 1;
        for (int c = 0; (c < copies) && !s.bad; c++) {
            for (int i = 0; (i < nunit) && !s.bad; i++) {
                int f;

                if (x87_decode(&unit[i], &op, &modrm, &at, &pfx))
                    continue;
                f = x87_effect(&s, op, modrm);
                if ((f != XM_NONE) && (fmt == XM_NONE)) {
                    fmt   = f;
                    env16 = pfx & 1;
                    addr  = x87_mem_addr(&unit[i], at, pfx & 2);
                }
            }
        }
        if (!s.bad)
            depth = d;
    }
    if (depth < 0)
        return -1;

    memset(x, 0, sizeof(*x));
    x->npxc = 0x037f;
    if (!strcmp(cond, "pc24"))
        x->npxc &= ~0x0300;
    else if (!strcmp(cond, "pc53"))
        x->npxc = (x->npxc & ~0x0300) | 0x0200;
    x->top = 0;
    for (int i = 0; i < 8; i++) {
        val[i]     = small ? (0.125 + 0.0625 * i) : (1.5 + 0.25 * i);
        x->st[i]   = val[i];
        x->tag[i]  = (i < depth) ? TAG_VALID : TAG_EMPTY;
    }
    memset(sd, 0, sizeof(*sd));
    sd->addr = addr;
    switch (fmt) {
        case XM_R32:
            x87_put(sd->pat, 0x3fc00000, sd->len = 4);
            break;
        case XM_R64:
            x87_put(sd->pat, 0x3ff8000000000000ULL, sd->len = 8);
            break;
        case XM_R80:
            x87_put80(sd->pat, 1.5);
            sd->len = 10;
            break;
        case XM_I16:
            x87_put(sd->pat, 3, sd->len = 2);
            break;
        case XM_I32:
            x87_put(sd->pat, 3, sd->len = 4);
            break;
        case XM_I64:
            x87_put(sd->pat, 3, sd->len = 8);
            break;
        case XM_BCD:
            x87_put(sd->pat, 3, sd->len = 10);
            break;
        case XM_CW:
            x87_put(sd->pat, x->npxc, sd->len = 2);
            break;
        case XM_ENV:
        case XM_SAVE:
            /* the protected mode image, 32-bit (28 bytes) or under an
               operand-size prefix 16-bit (14 bytes): control, status (TOP
               0), tag word (every register valid), the pointers, then for
               FRSTOR ST(0) .. ST(7) */
            {
                const int w = env16 ? 2 : 4;

                x87_put(sd->pat, x->npxc, w);
                memset(sd->pat + w, 0, 6 * w);
                sd->len = 7 * w;
                if (fmt == XM_SAVE) {
                    for (int i = 0; i < 8; i++)
                        x87_put80(sd->pat + 7 * w + 10 * i, val[i]);
                    sd->len = 7 * w + 80;
                }
            }
            break;
        default:
            sd->len = 0;
            break;
    }
    if (!sd->addr)
        sd->len = 0;
    sd->plen = sd->len;
    return 1;
}

/* The end-to-end run of a body of instances. */
static int
wire_insts(const ins_t *const *s, int ns, uint32_t ecx, uint32_t eflags, const meter_x87_t *x87, const meter_seed_t *seeds,
           int nseeds, uint64_t mm0, meter_wire_t *w)
{
    static uint8_t buf[BUF_MAX];
    int            len = 0;

    for (int i = 0; i < ns; i++) {
        if (len + s[i]->len > HARNESS_BODY_MAX) {
            snprintf(w->why, sizeof(w->why), "toolong");
            return -1;
        }
        memcpy(buf + len, s[i]->b, s[i]->len);
        len += s[i]->len;
    }
    wire_run_count++;
    return harness_meter_wire(buf, len, ecx, eflags, x87, seeds, nseeds, mm0, w);
}

static int
wire_body(const ins_t *unit, int nunit, int copies, uint32_t ecx, uint32_t eflags, const meter_x87_t *x87,
          const meter_seed_t *seeds, int nseeds, uint64_t mm0, meter_wire_t *w)
{
    static const ins_t *s[NWARM + STREAM_MAX * UNIT_MAX];
    int                 n = 0;

    for (int i = 0; i < NWARM; i++)
        s[n++] = &warm[i];
    for (int c = 0; (c < copies) && (n + nunit <= NWARM + STREAM_MAX * UNIT_MAX); c++) {
        for (int i = 0; i < nunit; i++)
            s[n++] = &unit[i];
    }
    return wire_insts(s, n, ecx, eflags, x87, seeds, nseeds, mm0, w);
}

/* MM0 as the run starts it: 0 (harness_meter_wire()), or 1.0 in both
   single lanes where the unit's PFRCP or PFRSQRT reads it, whose estimate
   of 0 is an infinity, an unsupported operand for the 3DNow! ops after it
   (AMD 21928G p. 10). Counted in seed_mm0_count. */
static int seed_mm0_count;

static uint64_t
mm0_seed(const ins_t *unit, int nunit)
{
    for (int i = 0; i < nunit; i++) {
        const ins_t *in = &unit[i];
        int          k  = 0;

        while ((k < in->len) && is_prefix_byte(in->b[k]))
            k++;
        if ((k + 3 < in->len) && (in->b[k] == 0x0f) && (in->b[k + 1] == 0x0f) && ((in->b[k + 2] & 0xc7) == 0xc0) &&
            ((in->b[in->len - 1] == 0x96) || (in->b[in->len - 1] == 0x97))) {
            seed_mm0_count++;
            return 0x3f8000003f800000ull;
        }
    }
    return 0;
}

/* A REP string op the run reaches with ECX above 1: the compile pass runs
   one element and ends the block there (x86_ops_rep_dyn.h), which the
   meter does not follow. */
static int
rep_with_count(const ins_t *unit, int nunit, uint32_t ecx)
{
    for (int i = 0; (i < nunit) && (ecx > 1); i++) {
        int rep = 0;
        int k   = 0;

        for (; (k < unit[i].len) && is_prefix_byte(unit[i].b[k]); k++)
            rep |= (unit[i].b[k] == 0xf2) || (unit[i].b[k] == 0xf3);
        if (rep && (k < unit[i].len) &&
            (((unit[i].b[k] >= 0x6c) && (unit[i].b[k] <= 0x6f)) || ((unit[i].b[k] >= 0xa4) && (unit[i].b[k] <= 0xa7)) ||
             ((unit[i].b[k] >= 0xaa) && (unit[i].b[k] <= 0xaf))))
            return 1;
    }
    return 0;
}

/* model: the isolated block (single: 1 minus 0 copies; else hi minus lo);
   ctx and wire: hi minus lo copies inside the harness's frame. path: 1 the
   row's conditional branches are taken, 0 not taken, -1 the row names no
   path (the model figure takes none; the end-to-end run goes the way the
   flags it starts with send it, clear but for the reserved bit, ECX 1).
   For a row holding a conditional branch, ctx is also taken on the other
   path (ctx_alt): which of the two the run matches says the path it took.
   The run times each instance in its wire_form(), the flags and ECX set by
   path_state() for a named path. frame 0 or 1: ctx and wire are the
   instance alone in single_stream()'s frame, with it minus without it. */
static void
measure(const ins_t *unit, int nunit, int single, int lo, int hi, int div, int path, int run_wire, const char *cond,
        int frame, meas_t *m)
{
    static const ins_t *stream[STREAM_MAX * UNIT_MAX];
    ins_t               wunit[UNIT_MAX];
    walk_t              w;
    walk_t              wb_walk = { 0 };
    int                 cond_op = 0; /* a branch, CMOVcc or SETcc whose path the row names */
    int                 ahead   = 0; /* single_stream()'s separators ahead */
    int                 taken  = (path == 1);
    uint32_t            eflags = 0x0002;
    uint32_t            ecx    = 1;
    int64_t             a;
    int64_t             b;
    int                 n;

    memset(m, 0, sizeof(*m));
    m->mdiv = single ? 1 : div;
    m->div  = div;
    n       = build(NULL, 0, unit, nunit, single ? 0 : lo, 0, stream);
    if (!drive(stream, n, taken, 0, &a)) {
        n = build(NULL, 0, unit, nunit, single ? 1 : hi, 0, stream);
        if (!drive(stream, n, taken, 0, &b)) {
            m->has_model = 1;
            m->model_t   = b - a;
            m->model     = per_x100(m->model_t, m->mdiv);
        }
    }
    if (!m->has_model)
        snprintf(m->why, sizeof(m->why), "fatal");
    /* ctx is taken for every row, the end-to-end run only where the row's
       cond runs in the harness; an instance the body cannot keep (a far,
       indirect or return branch, an interrupt) is not run, and ctx follows
       it as it is */
    for (int i = 0; i < nunit; i++) {
        if (wire_form(&unit[i], taken, &wunit[i], &w)) {
            wunit[i] = unit[i];
            if (run_wire)
                snprintf(m->why, sizeof(m->why), "leavesbody");
            run_wire = 0;
        }
        if (branch_kind(&w) == B_COND) {
            m->cond_branch = 1;
            wb_walk        = w;
            cond_op        = 1;
        } else if (!cond_op && (w.last_prefix == 0x0f) && (((w.opcode & 0xf0) == 0x40) || ((w.opcode & 0xf0) == 0x90))) {
            /* CMOVcc and SETcc: the row's path is their condition's, on the
               Jcc condition codes */
            wb_walk = w;
            cond_op = 1;
        }
    }
    if (cond_op && (path >= 0) && path_state(&wb_walk, path, &eflags, &ecx)) {
        snprintf(m->why, sizeof(m->why), "nopath");
        return;
    }
    if (rep_with_count(unit, nunit, ecx)) {
        m->has_model = 0;
        snprintf(m->mwhy, sizeof(m->mwhy), "repecx");
        snprintf(m->why, sizeof(m->why), "repecx");
        return;
    }
    if (m->cond_branch && (path < 0) && !wb_walk.last_prefix && ((wb_walk.opcode & 0xfc) == 0xe0) && (wb_walk.opcode != 0xe3)) {
        snprintf(m->why, sizeof(m->why), "nopath"); /* LOOPcc: its path moves with ECX copy by copy */
        return;
    }
    /* a copy ahead that ends a block (an MMX op entering MMX mode) moves
       the timed instance off the limit: another count of separators puts
       it back, the fewest that do */
    if (frame >= 0) {
        const int amax = single_ahead_max(wunit, nunit);

        for (int k = 1; k <= 2 * MAX_INSTRUCTION_COUNT; k++) {
            ahead           = (k == 1) ? amax : k;
            n               = single_stream(wunit, nunit, ahead, 1, frame, 1, stream);
            drive_mark      = single_at;
            drive_mark_last = 0;
            if ((n >= 0) && (drive(stream, n, taken, 1, &b) || drive_mark_last))
                break;
            ahead = amax;
        }
        drive_mark = -1;
    }
    n = (frame < 0) ? build(NULL, 0, wunit, nunit, lo, 1, stream) : single_stream(wunit, nunit, ahead, 0, frame, 1, stream);
    if ((n >= 0) && !drive(stream, n, taken, 1, &a)) {
        n = (frame < 0) ? build(NULL, 0, wunit, nunit, hi, 1, stream) : single_stream(wunit, nunit, ahead, 1, frame, 1, stream);
        drive_mark      = (frame < 0) ? -1 : single_at;
        drive_mark_last = 0;
        if (!drive(stream, n, taken, 1, &b)) {
            m->alone   = drive_mark_last;
            m->has_ctx = 1;
            m->ctx_t   = b - a;
            m->ctx     = per_x100(m->ctx_t, div);
        }
        drive_mark = -1;
    }
    if (m->cond_branch) {
        n = (frame < 0) ? build(NULL, 0, wunit, nunit, lo, 1, stream) : single_stream(wunit, nunit, ahead, 0, frame, 1, stream);
        if ((n >= 0) && !drive(stream, n, !taken, 1, &a)) {
            n = (frame < 0) ? build(NULL, 0, wunit, nunit, hi, 1, stream) : single_stream(wunit, nunit, ahead, 1, frame, 1, stream);
            if (!drive(stream, n, !taken, 1, &b)) {
                m->has_ctx_alt = 1;
                m->ctx_alt     = per_x100(b - a, div);
            }
        }
    }
    if (!run_wire)
        return;
    /* ENTER whose frames take the hi run's copies below the stack page the
       warm-up filled: each new page's first access charges its fill, the
       run's and not the instruction's */
    for (int i = 0; i < nunit; i++) {
        const ins_t *in = &unit[i];

        if ((in->len == 4) && (in->b[0] == 0xc8) &&
            ((uint32_t) hi * ((in->b[1] | (in->b[2] << 8)) + 4u * ((in->b[3] & 0x1f) + 1)) > (HARNESS_ESP0 - HARNESS_STACK))) {
            snprintf(m->why, sizeof(m->why), "stackpage");
            return;
        }
    }
    {
        meter_wire_t wa = { 0 };
        meter_wire_t wb = { 0 };
        meter_x87_t    x87;
        meter_seed_t   sd;
        const int      xs  = x87_setup(wunit, nunit, hi, cond, &x87, &sd);
        const uint64_t mm0 = mm0_seed(wunit, nunit);

        if (xs < 0) {
            snprintf(m->why, sizeof(m->why), "x87depth");
            return;
        }
        if (frame >= 0) {
            static const ins_t *s0[STREAM_MAX * UNIT_MAX];
            static const ins_t *s1[STREAM_MAX * UNIT_MAX];
            const int           n0 = single_stream(wunit, nunit, ahead, 0, frame, 0, s0);
            const int           n1 = single_stream(wunit, nunit, ahead, 1, frame, 0, s1);

            if ((n0 < 0) || wire_insts(s0, n0, ecx, eflags, xs ? &x87 : NULL, &sd, xs ? 1 : 0, mm0, &wa)
                || wire_insts(s1, n1, ecx, eflags, xs ? &x87 : NULL, &sd, xs ? 1 : 0, mm0, &wb)) {
                snprintf(m->why, sizeof(m->why), "%s", wa.why[0] ? wa.why : wb.why[0] ? wb.why : "noroom");
                return;
            }
        } else if (wire_body(wunit, nunit, lo, ecx, eflags, xs ? &x87 : NULL, &sd, xs ? 1 : 0, mm0, &wa)
                   || wire_body(wunit, nunit, hi, ecx, eflags, xs ? &x87 : NULL, &sd, xs ? 1 : 0, mm0, &wb)) {
            snprintf(m->why, sizeof(m->why), "%s", wa.why[0] ? wa.why : wb.why);
            return;
        }
        m->has_wire = 1;
        m->wc_t     = (int64_t) wb.compiled - (int64_t) wa.compiled;
        m->wi_t     = (int64_t) wb.interp - (int64_t) wa.interp;
        m->wc       = per_x100(m->wc_t, div);
        m->wi       = per_x100(m->wi_t, div);
    }
}

/* What a REP CMPS or SCAS row's runs compare, so the instruction runs
   every element (ECX of them, up to n): for REPE equal elements (ESI's and
   EDI's the same bytes; EDI's equal to EAX for SCAS), for REPNE unequal
   ones (ESI's 11h bytes against EDI's 22h; FFh bytes for SCAS, which EAX,
   a data page address, never is). ESI and EDI are the harness's (the data
   page and 400h into it), DF clear. Returns the seeds' count. */
static int
rep_seeds(const ins_t *in, int n, meter_seed_t *s)
{
    int     i    = 0;
    int     rep  = 0;
    int     o16  = 0;
    int     size = 4;
    uint8_t op;

    for (; (i < in->len) && is_prefix_byte(in->b[i]); i++) {
        if ((in->b[i] == 0xf2) || (in->b[i] == 0xf3))
            rep = in->b[i];
        else if (in->b[i] == 0x66)
            o16 = 1;
    }
    if (!rep || (i >= in->len))
        return 0;
    op = in->b[i];
    if ((op != 0xa6) && (op != 0xa7) && (op != 0xae) && (op != 0xaf))
        return 0;
    if (!(op & 1))
        size = 1;
    else if (o16)
        size = 2;
    memset(s, 0, 2 * sizeof(*s));
    s[0].addr = HARNESS_DATA + 0x400;
    s[0].len  = n * size;
    s[0].plen = 1;
    s[1].addr = HARNESS_DATA;
    s[1].len  = n * size;
    s[1].plen = 1;
    if ((op == 0xae) || (op == 0xaf)) {
        if (rep == 0xf3) {
            const uint32_t eax = HARNESS_DATA + 0x100;

            for (int k = 0; k < size; k++)
                s[0].pat[k] = (uint8_t) (eax >> (8 * k));
            s[0].plen = size;
        } else
            s[0].pat[0] = 0xff;
        return 1;
    }
    s[0].pat[0] = (rep == 0xf3) ? 0x5a : 0x22;
    s[1].pat[0] = (rep == 0xf3) ? 0x5a : 0x11;
    return 2;
}

/* rep:n1,n2: per element from the end-to-end runs. */
static void
measure_rep(const ins_t *unit, int n1, int n2, meas_t *m)
{
    meter_wire_t a = { 0 };
    meter_wire_t b = { 0 };
    meter_seed_t sd[2];
    const int    ns = rep_seeds(unit, n2, sd);

    if (wire_body(unit, 1, 1, n1, 0x0002, NULL, sd, ns, 0, &a) || wire_body(unit, 1, 1, n2, 0x0002, NULL, sd, ns, 0, &b)) {
        snprintf(m->why, sizeof(m->why), "%s", a.why[0] ? a.why : b.why);
        return;
    }
    m->has_wire = 1;
    m->wc       = per_x100((int64_t) b.compiled - (int64_t) a.compiled, n2 - n1);
    m->wi       = per_x100((int64_t) b.interp - (int64_t) a.interp, n2 - n1);
}

/* A row of prefix bytes alone: timed on a carrier that takes it (a memory
   operand for a segment override, a lockable one for LOCK, a register one
   for the rest, where an operand-size, address-size or REP prefix changes
   nothing a timing model reads), one whose copies read none of each
   other's values, or for a latency (chain 1) one whose copies each read
   the one before. */
static const ins_t *
prefix_carrier(const ins_t *in, int chain)
{
    static const ins_t mem   = { { 0x8b, 0x03 }, 2 }; /* mov eax,[ebx] */
    static const ins_t mem_c = { { 0x8b, 0x00 }, 2 }; /* mov eax,[eax] */
    static const ins_t lock  = { { 0x01, 0x03 }, 2 }; /* add [ebx],eax */
    static const ins_t reg   = { { 0x89, 0xc8 }, 2 }; /* mov eax,ecx */
    static const ins_t reg_c = { { 0x01, 0xc0 }, 2 }; /* add eax,eax */
    int                seg   = 0;
    int                lk    = 0;

    for (int i = 0; i < in->len; i++) {
        if (!is_prefix_byte(in->b[i]))
            return NULL;
        if (in->b[i] == 0xf0)
            lk = 1;
        else if ((in->b[i] != 0x66) && (in->b[i] != 0x67) && (in->b[i] != 0xf2) && (in->b[i] != 0xf3))
            seg = 1;
    }
    return lk ? &lock : seg ? (chain ? &mem_c : &mem) : (chain ? &reg_c : &reg);
}

/* A Pentium x87 latency row whose book gives the latency and throughput
   as one figure: a single count (F.3.2 writes a second, after a slash, only
   where pipelining makes the throughput differ) or "a/b" with a = b. Its
   copies, where they read none of each other's values, are timed as they
   are: one copy every latency clocks either way. */
static int
fp_same(const row_t *r)
{
    int64_t a;
    int64_t b;

    if (r->fp == FP_ONE)
        return 1;
    return (r->fp == FP_LAT) && parse_x100(r->fp_a, r->fp_a + strlen(r->fp_a), &a) &&
           parse_x100(r->fp_b, r->fp_b + strlen(r->fp_b), &b) && (a == b);
}

/* An "a/b" row's a / b rounded up: how many copies back a /tput stream's
   copies may read a value (x87_stream()). */
static int
fp_need(const row_t *r)
{
    int64_t a;
    int64_t b;

    if ((r->fp != FP_TPUT) || !parse_x100(r->fp_a, r->fp_a + strlen(r->fp_a), &a) ||
        !parse_x100(r->fp_b, r->fp_b + strlen(r->fp_b), &b) || (b <= 0))
        return 8;
    return (int) ((a + b - 1) / b);
}

/* ---- charges outside the model ---------------------------------------- */

/* What compiled code charges for a unit beyond the model's hooks, per copy
   of the unit (c, its terms in names), read from the variables the code
   charges, or why the meter does not reproduce it (why: the row is
   OFFMODEL). mode: the row's cond (rm, v86, cpl3; anything else runs in
   the harness's flat ring 0 protected mode). */
typedef struct offm_t {
    int64_t c;
    char    names[128];
    char    why[80];
    char    unread[80]; /* offm_unread(); a rejected run's reason is printed first */
} offm_t;

static void
offm_add(offm_t *o, int64_t c, const char *name)
{
    if (!c)
        return;
    o->c += c;
    meter_putf(o->names, sizeof(o->names), "%s%s=%" PRId64, o->names[0] ? "+" : "", name, c);
}

static void
offm_no(offm_t *o, const char *why)
{
    if (!o->why[0])
        snprintf(o->why, sizeof(o->why), "%s", why);
}

/* A charge the op writes as a literal or an expression of its own (opHLT's
   CLOCK_CYCLES_ALWAYS(100), opCMPXCHG8B's cycles -= 10, the RCL/RCR count
   charge): no variable holds it, so a copy here would not follow a change
   to the op. */
static void
offm_unread(offm_t *o, const char *name)
{
    if (!o->unread[0])
        snprintf(o->unread, sizeof(o->unread), "outside-charge-not-readable(%s)", name);
}

/* A data access of size bytes at linear address a: the charge readmem*()
   and writemem*() (mem.c) and the recompiler's in-page paths make for a
   misaligned one, timing_misaligned. With cpu_cyrix_alignment set, which
   misaligned accesses are charged is decided by a test written inline in
   mem.c, not readable here: such an access is left unreproduced. */
static int
misaligned_cycles(offm_t *o, uint32_t a, int size)
{
    if (!(((size == 2) && (a & 1)) || ((size == 4) && (a & 3))))
        return 0;
    if (cpu_cyrix_alignment) {
        offm_unread(o, "cyrix_alignment");
        return 0;
    }
    return timing_misaligned;
}

static void
off_model_one(const ins_t *in, const char *cond, offm_t *o)
{
    const int real = !strcmp(cond, "rm");
    const int v86  = !strcmp(cond, "v86");
    const int cpl3 = !strcmp(cond, "cpl3");
    ins_t     h;
    walk_t    w;
    uint8_t   op;
    int       reg;
    int       mem;
    int       at;
    int       far = 0;

    past_fwait(in, &h);
    place(h.b, h.len);
    walk(HARNESS_CODE, 0, &w);
    unplace();
    op  = w.opcode;
    reg = (w.fetchdat >> 3) & 7;
    mem = ((w.fetchdat & 0xc0) != 0xc0);
    at  = (int) (w.op_pc - HARNESS_CODE);
    if ((w.last_prefix & 0xf8) == 0xd8) {
        /* FRSTOR, FNSAVE: the image's fields at 10-byte steps, each
           misaligned access charged timing_misaligned (mem.c): not
           counted here */
        if ((w.last_prefix == 0xdd) && (op < 0xc0) && ((((op >> 3) & 7) == 4) || (((op >> 3) & 7) == 6)))
            offm_no(o, "misaligned-image-accesses");
        return;
    }
    if (w.last_prefix == 0x0f) {
        if ((op == 0xc7) && (reg == 1) && mem)
            offm_unread(o, "cmpxchg8b"); /* opCMPXCHG8B, x86_ops_atomic.h: the memory form's cycles -= 10 */
        else if ((op == 0x01) && (reg < 4) && mem) {
            /* SGDT, SIDT, LGDT, LIDT: the limit word, then the base dword at
               +2 */
            const uint32_t a = x87_mem_addr(&h, at, !(w.op_32 & 0x200));

            if (!a)
                offm_no(o, "operand-address-not-set");
            else
                offm_add(o, misaligned_cycles(o, a, 2) + misaligned_cycles(o, a + 2, 4), "timing_misaligned");
        }
        return;
    }
    if (w.last_prefix)
        return;
    if ((op == 0x9a) || ((op == 0xff) && (reg == 3) && mem))
        far = 1; /* CALL far */
    else if ((op == 0xea) || ((op == 0xff) && (reg == 5) && mem))
        far = 2; /* JMP far */
    else if ((op == 0xca) || (op == 0xcb))
        far = 3; /* RETF */
    else if (op == 0xcf)
        far = 4; /* IRET */
    else if ((op == 0xcc) || (op == 0xcd) || (op == 0xf1) || ((op == 0xce) && !strcmp(cond, "taken")))
        far = 5; /* INT3, INT n, INT1, INTO trapping */
    if ((op == 0xce) && (real || v86 || cpl3))
        offm_no(o, "into-row-names-no-path");
    else if (far && (v86 || cpl3))
        offm_no(o, "far-transfer-not-run-in-mode");
    else if (far) {
        /* real mode: the op's or the compiled form's own charge
           (x86_ops_call.h, codegen_ops_jump.c, x86seg.c loadcsjmp(),
           x86_ops_ret.h, 386_dynarec_ops.c IRET, 386_common.c
           x86_int_sw()); protected mode: the same privilege, no gate
           (x86seg.c loadcscall(), loadcsjmp(), pmoderetf(), pmodeiret(),
           pmodeint()), the harness's flat ring 0 */
        static const char *const rn[6] = { "", "timing_call_rm", "timing_jmp_rm", "timing_retf_rm", "timing_iret_rm", "timing_int_rm" };
        static const char *const pn[6] = { "", "timing_call_pm", "timing_jmp_pm", "timing_retf_pm", "timing_iret_pm", "timing_int_pm" };
        const int rv[6] = { 0, timing_call_rm, timing_jmp_rm, timing_retf_rm, timing_iret_rm, timing_int_rm };
        const int pv[6] = { 0, timing_call_pm, timing_jmp_pm, timing_retf_pm, timing_iret_pm, timing_int_pm };

        if (far == 5)
            offm_add(o, timing_int, "timing_int");
        if (real)
            offm_add(o, rv[far], rn[far]);
        else
            offm_add(o, pv[far], pn[far]);
    } else if ((((op >= 0xe4) && (op <= 0xe7)) || ((op >= 0xec) && (op <= 0xef)) || ((op >= 0x6c) && (op <= 0x6f))) && (v86 || cpl3))
        offm_no(o, "io-permission-path-not-run");
    else if (((op == 0x9c) || (op == 0x9d) || (op == 0xfa) || (op == 0xfb)) && v86)
        offm_no(o, "iopl-path-not-run");
    else if (op == 0xf4) {
        if (v86 || cpl3)
            offm_no(o, "hlt-faults-in-mode");
        else
            offm_unread(o, "hlt"); /* opHLT, x86_ops_misc.h */
    } else if (((op == 0xc0) || (op == 0xc1) || (op == 0xd2) || (op == 0xd3)) && ((reg == 2) || (reg == 3))) {
        /* RCL, RCR by an immediate or CL: the recompiler leaves them to the
           op (codegen_ops_shift.c), which charges the count itself on the
           486 and later (x86_ops_shift.h). By 1 (D0, D1) they are compiled
           (rcx1_common()), no op call. */
        if ((op >= 0xd2) && !mem && ((w.fetchdat & 7) == 1))
            offm_no(o, "rcl-rcr-count-in-cl-changes");
        else
            offm_unread(o, "rcl_rcr_count");
    }
}

/* The penalty of a misaligned operand (cond misaligned), per copy: the
   model's part is 0 (no hook is handed an address), the compiled code's
   timing_misaligned for each access to the operand (a load reads it, a
   store writes it, a read-modify-write does both), at an address its
   CPU's rule charges. */
static void
misaligned_penalty(const ins_t *in, offm_t *o)
{
    ins_t   h;
    walk_t  w;
    uint8_t op;
    int     acc = 0;

    past_fwait(in, &h);
    place(h.b, h.len);
    walk(HARNESS_CODE, 0, &w);
    unplace();
    op = w.opcode;
    if (w.last_prefix || ((w.fetchdat & 0xc0) == 0xc0))
        offm_no(o, "accesses-not-counted");
    else if ((op == 0x88) || (op == 0x8a) || ((op < 0x40) && ((op & 7) < 4) && !(op & 1)))
        offm_no(o, "byte-operand-never-misaligned");
    else if ((op == 0x8b) || (op == 0x89) || ((op < 0x40) && ((op & 7) == 3)) || ((op < 0x40) && ((op & 0x38) == 0x38) && ((op & 7) == 1)))
        acc = 1;
    else if ((op < 0x40) && ((op & 7) == 1))
        acc = 2;
    else
        offm_no(o, "accesses-not-counted");
    if (acc && cpu_cyrix_alignment)
        offm_unread(o, "cyrix_alignment"); /* misaligned_cycles() */
    else if (acc)
        offm_add(o, acc * timing_misaligned, "timing_misaligned");
}

/* The instances as the end-to-end run times them (wire_form()); -1 when
   one cannot be kept in the body. */
static int
wire_unit(const ins_t *unit, int nunit, int taken, ins_t *out)
{
    for (int i = 0; i < nunit; i++) {
        if (wire_form(&unit[i], taken, &out[i], NULL))
            return -1;
    }
    return 0;
}

/* A row whose end-to-end run was made and turned down for what it showed
   (the runs disagreeing or not settling, the case not ending at the
   epilogue, a harness fault): what compiled code charges it is neither
   measured nor shown to be ctx's. A run not made (a far transfer, a fault
   on the harness's operands, a stack past its page, an x87 stack too deep)
   leaves ctx plus the outside charges as the reproduction. */
static int
run_rejected(const char *cond, const meas_t *m)
{
    static const char *const rej[] = { "state", "notconv", "timing", "harness", "noepilogue", "reset", "timeout", "toolong" };

    if (!cond_runs(cond) || m->has_wire)
        return 0;
    for (size_t i = 0; i < sizeof(rej) / sizeof(rej[0]); i++) {
        if (!strcmp(m->why, rej[i]))
            return 1;
    }
    return 0;
}

/* A row's unit measured (measure()), a prefix row as its prefixed
   carrier minus the carrier: the difference of the totals, then per
   copy. */
static void
measure_unit(const ins_t *unit, int nunit, const ins_t *carrier, const ins_t *pfx, int single, int lo, int hi, int div,
             int path, int run, const char *cond, int frame, meas_t *m)
{
    meas_t mc;

    if (!carrier) {
        measure(unit, nunit, single, lo, hi, div, path, run, cond, frame, m);
        return;
    }
    measure(pfx, 1, single, lo, hi, div, path, run, cond, frame, m);
    measure(carrier, 1, single, lo, hi, div, path, run, cond, frame, &mc);
    if (m->has_model && !mc.has_model)
        snprintf(m->mwhy, sizeof(m->mwhy), "%s", mc.mwhy);
    m->has_model &= mc.has_model;
    m->model_t -= mc.model_t;
    m->model = per_x100(m->model_t, m->mdiv);
    m->has_ctx &= mc.has_ctx;
    m->alone &= mc.alone;
    m->ctx_t -= mc.ctx_t;
    m->ctx = per_x100(m->ctx_t, div);
    if (m->has_wire && !mc.has_wire)
        snprintf(m->why, sizeof(m->why), "%s", mc.why);
    m->has_wire &= mc.has_wire;
    m->wc_t -= mc.wc_t;
    m->wi_t -= mc.wi_t;
    m->wc = per_x100(m->wc_t, div);
    m->wi = per_x100(m->wi_t, div);
}

/* ---- one row --------------------------------------------------------- */

static void
fmt_opt(char *out, size_t n, int has, int64_t v, const char *none)
{
    if (has)
        fmt_x100(out, n, v);
    else
        snprintf(out, n, "%s", none);
}

static void
meter_row(const row_t *r, const char *target, int no_wire, int rider, int nobook)
{
    ins_t       unit[UNIT_MAX];
    ins_t       first[UNIT_MAX];
    ins_t       head;
    int         nunit;
    int         nfirst;
    int         dep_need  = 0; /* 1 a latency's chain, 2 a throughput's stream */
    int         bad_dep   = 0; /* V_NOCHAIN or V_NOSTREAM */
    const char *kind      = r->f[C_KIND];
    const char *status    = r->f[C_STATUS];
    const char *cond      = r->f[C_COND];
    int         per_ins   = 0;
    int         single    = 0;
    int         sil       = !strncasecmp(status, "silicon", 7);
    int         is_rep    = !strncmp(kind, "rep:", 4);
    int         rep1      = 0;
    int         rep2      = 0;
    int         rep_fixed = 0;
    int         taken     = !strcmp(cond, "taken");
    char        st[512];
    char        model[512] = "-";
    char        ctx[32]    = "-";
    char        wc[32]     = "-";
    char        wi[32]     = "-";
    char        delta[300] = "-";
    char        dir[16]    = "-";
    char        dir_i[16]  = "-";
    char        delta_i[64];
    char        gap[16]    = "-";
    char        code_row[200] = "-";
    char        code_row_i[200] = "-";
    char        code_row_d[200] = "-";
    char        slope[32];
    char        row[160];
    const char *book      = r->f[C_BOOK];
    int         verdict   = V_ERROR;
    int         verdict_i = V_NORUN;
    meas_t      m;
    walk_t      w;
    offm_t      om;
    int64_t     xm        = 0; /* om per copy x100: the model figure's terms, ctx's */
    int64_t     xc        = 0;
    const char *why_un    = NULL; /* an unscored row's reason */
    const char *dep_why   = NULL;
    int         frame_dep = 0; /* a single row's charge differs between the two frames */
    char        frame_why[96];

    memset(&m, 0, sizeof(m));
    memset(&om, 0, sizeof(om));
    nfirst = parse_units(r->f[C_BYTES], first, UNIT_MAX);
    if (nfirst <= 0) {
        snprintf(model, sizeof(model), "bytes?");
        goto print;
    }
    memcpy(unit, first, sizeof(ins_t) * nfirst);
    nunit = nfirst;
    if (!strncmp(kind, "pair:", 5)) {
        int more = parse_units(kind + 5, unit + nunit, UNIT_MAX - nunit);

        if (more <= 0) {
            snprintf(model, sizeof(model), "kind?");
            goto print;
        }
        nunit += more;
    } else if (!strncmp(kind, "seq:", 4)) {
        nunit = parse_units(kind + 4, unit, UNIT_MAX);
        if (nunit <= 0) {
            snprintf(model, sizeof(model), "kind?");
            goto print;
        }
    } else if (is_rep) {
        /* rep:<n1>,<n2>, or rep:<count> for a count in ECX (c, n): 4 and 12.
           Another count (ENTER's level, a far CALL's parameters) is not
           varied: the model's charge is shown, the formula unscored. */
        if (sscanf(kind + 4, "%d,%d", &rep1, &rep2) == 2) {
            if ((rep1 < 0) || (rep2 <= rep1) || (rep2 > 1024)) {
                snprintf(model, sizeof(model), "kind?");
                goto print;
            }
        } else if (!strcmp(kind + 4, "c") || !strcmp(kind + 4, "n")) {
            rep1 = 4;
            rep2 = 12;
        } else
            rep_fixed = 1;
        nunit = 1;
    } else if (!strcmp(kind, "chain") || (!strcmp(kind, "single") && !strcmp(r->f[C_MEANING], "latency"))) {
        per_ins  = 1; /* a latency is a dependent chain's figure */
        dep_need = 1;
    } else if (!strcmp(kind, "stream") || (!strcmp(kind, "single") && !strcmp(r->f[C_MEANING], "throughput"))) {
        per_ins  = 1; /* a throughput an independent stream's */
        dep_need = 2;
    } else if (!strcmp(kind, "single"))
        single = 1;
    else if (strncmp(kind, "struct", 6)) {
        snprintf(model, sizeof(model), "kind?");
        goto print;
    }

    /* A row of prefix bytes alone is read and timed on its carrier: the
       table entry the prefixed carrier reaches, beside the prefix's own
       row (code_row prefix[..]). */
    {
        const ins_t *carrier = (nunit == 1) ? prefix_carrier(&unit[0], dep_need == 1) : NULL;

        if (carrier) {
            ins_t pfx;

            memcpy(pfx.b, unit[0].b, unit[0].len);
            memcpy(pfx.b + unit[0].len, carrier->b, carrier->len);
            pfx.len = unit[0].len + carrier->len;
            read_struct(&pfx, st, sizeof(st), &w);
            meter_putf(st, sizeof(st), ";carrier=%02x%s%02x", carrier->b[0], (carrier->len > 1) ? "." : "",
                       (carrier->len > 1) ? carrier->b[1] : 0);
        } else {
            past_fwait(&first[0], &head);
            read_struct(&head, st, sizeof(st), &w);
        }
    }
    past_fwait(&first[0], &head);
    interp_row(&head, &w, code_row_i, sizeof(code_row_i));
    if (kv_get(st, "deps", row, sizeof(row)))
        snprintf(code_row_d, sizeof(code_row_d), "%s", row);
    if (r->f[C_CODEROW][0])
        snprintf(code_row, sizeof(code_row), "%s", r->f[C_CODEROW]);
    else if (kv_get(st, "row", row, sizeof(row)))
        snprintf(code_row, sizeof(code_row), "%s:%s", model_name, row);
    if ((nunit == 1) && prefix_carrier(&unit[0], 0)) {
        snprintf(code_row, sizeof(code_row), "%s:prefix[%02x]", model_name, unit[0].b[unit[0].len - 1]);
        snprintf(code_row_i, sizeof(code_row_i), "interp:prefix.%02x", unit[0].b[unit[0].len - 1]);
    }

    if (is_struct_row(r)) {
        snprintf(model, sizeof(model), "%s", st);
        if (!strcmp(r->f[C_MEANING], "pairing") && kv_get(st, "prefix_delay", row, sizeof(row)))
            snprintf(code_row, sizeof(code_row), "%s:prefix_decode_delay[%s]", model_name, row);
        if (!strcasecmp(status, "no-book") || !strcmp(book, "-") || !book[0])
            verdict = V_NO_BOOK;
        else if (!strncasecmp(status, "split", 5)) {
            verdict = V_SPLIT;
            snprintf(delta, sizeof(delta), "%s", status); /* every document's value */
        } else if ((nunit == 1) && prefix_carrier(&unit[0], 0))
            verdict = V_NO_MODEL; /* no model's table holds an entry for a prefix by itself */
        else
            score_struct(r, st, sil, &verdict, delta, sizeof(delta));
        verdict_i = V_NORUN;
        goto print;
    }

    {
        const ins_t *carrier = (nunit == 1) ? prefix_carrier(&unit[0], dep_need == 1) : NULL;
        const int    run     = !no_wire && cond_runs(cond) && !rep_fixed;
        const int    path    = taken ? 1 : !strcmp(cond, "nottaken") ? 0 : -1;
        int          lo;
        int          hi;
        int          div;
        int          per;
        int          nx = nunit; /* the row's instructions in a unit, for a per-instruction figure */

        if (is_rep) {
            meas_t z;

            measure(unit, 1, 1, 2, 3, 1, path, 0, cond, -1, &m);
            if (run && !rep_fixed) {
                measure_rep(unit, rep1, rep2, &z);
                m.has_wire = z.has_wire;
                m.wc       = z.wc;
                m.wi       = z.wi;
                if (!z.has_wire)
                    snprintf(m.why, sizeof(m.why), "%s", z.why);
            }
            snprintf(gap, sizeof(gap), "rep");
        } else {
            ins_t  pfx;
            ins_t  built[UNIT_MAX];
            ins_t *form = carrier ? &pfx : unit; /* the instruction as it runs: a prefix on its carrier */

            drive_real    = !strcmp(cond, "rm") || !strcmp(cond, "v86");
            drive_skipped = 0;

            if (carrier) {
                memcpy(pfx.b, unit[0].b, unit[0].len);
                memcpy(pfx.b + unit[0].len, carrier->b, carrier->len);
                pfx.len = unit[0].len + carrier->len;
            }
            /* A latency row whose copies do not read each other is timed
               on the chain the instruction allows (rebuild_chain()); a
               Pentium x87 row whose book gives one latency and throughput
               (fp_same()) on its copies as they are; else as it is and not
               scored: NOCHAIN. A throughput row whose copies read each
               other is timed on a Pentium x87 /tput row's stream
               (x87_stream()), else not scored: NOSTREAM. The delta says
               which: needs-consumer, a Pentium x87 latency with a != b of
               an instruction whose copies read none of each other's values
               (it shows only as another instruction's wait); no-chain, any
               other latency the instruction cannot chain;
               book-pairs-no-fxch, a Pentium /tput row whose instruction the
               book does not pair with FXCH (NP, or no pairing row);
               copies-dependent, any other throughput. */
            if ((dep_need == 1) && !unit_chains(form, nunit)) {
                if ((carrier || (nunit != 1) || rebuild_chain(&unit[0])) && !fp_same(r)) {
                    bad_dep = V_NOCHAIN;
                    dep_why = (r->fp != FP_NONE) ? "needs-consumer" : "no-chain";
                }
            } else if ((dep_need == 2) && unit_chains(form, nunit)) {
                const int n = (!carrier && (nunit == 1) && (r->fp == FP_TPUT)) ? x87_stream(&unit[0], fp_need(r), r->fx, built, &nx) : 0;

                if (n) {
                    memcpy(unit, built, n * sizeof(ins_t));
                    nunit = n;
                } else {
                    bad_dep = V_NOSTREAM;
                    dep_why = ((r->fp == FP_TPUT) && !r->fx) ? "book-pairs-no-fxch" : "copies-dependent";
                }
            }
            if (single) {
                lo  = 2;
                hi  = 3;
                div = 1;
            } else {
                static int64_t t[4][BLOCK_FIT + 1];
                ins_t          wf[UNIT_MAX];
                ins_t          wcar;
                const int      ninsn = count_insns(form, nunit);
                int            maxc  = BLOCK_FIT / ninsn;
                int            ns    = 0;
                meter_x87_t    xc;
                meter_seed_t   xsd;

                /* no more copies than the x87 stack holds (x87_setup()) */
                while ((maxc > 3) && (x87_setup(form, nunit, maxc, "", &xc, &xsd) < 0))
                    maxc--;
                ns += !unit_totals(form, nunit, maxc, taken, 0, t[ns]);
                if (carrier)
                    ns += !unit_totals(carrier, 1, maxc, taken, 0, t[ns]);
                if (run && !wire_unit(form, nunit, taken, wf))
                    ns += !unit_totals(wf, nunit, maxc, taken, 1, t[ns]);
                if (run && carrier && !wire_unit(carrier, 1, taken, &wcar))
                    ns += !unit_totals(&wcar, 1, maxc, taken, 1, t[ns]);
                choose_window(t, ns, maxc, ninsn, &lo, &hi, &per);
                x87_fit(form, nunit, per, lo, &hi);
                div = (hi - lo) * (per_ins ? nx : 1);
            }
            if (single && strcmp(model_name, "486") && strcmp(model_name, "pentium") && strcmp(model_name, "686") &&
                strcmp(model_name, "winchip") && strcmp(model_name, "winchip2")) {
                /* an out-of-order model (single_stream()): the copies'
                   figures are shown, not scored; a row with no run is
                   scored from the empty block as before */
                measure_unit(unit, nunit, carrier, &pfx, single, lo, hi, div, path, run, cond, -1, &m);
                if (m.has_ctx || m.has_wire) {
                    snprintf(frame_why, sizeof(frame_why), "out-of-order-model-no-isolated-instance");
                    frame_dep = 1;
                }
            } else if (single) {
                meas_t m1;

                /* the instance alone (single_stream()): its figure must be
                   the isolated block's (model); the two frames' agreement
                   is counted */
                measure_unit(unit, nunit, carrier, &pfx, single, lo, hi, div, path, run, cond, 0, &m);
                measure_unit(unit, nunit, carrier, &pfx, single, lo, hi, div, path, run, cond, 1, &m1);
                if (!whole_insns(form, carrier ? 1 : nunit)) {
                    /* its opcode lies past its bytes: the instance takes
                       the next one's bytes, whatever follows it */
                    snprintf(frame_why, sizeof(frame_why), "instance-not-a-whole-instruction");
                    frame_dep = 1;
                } else if (m.has_ctx && !m.alone) {
                    snprintf(frame_why, sizeof(frame_why), "instance-shares-its-block");
                    frame_dep = 1;
                } else if (m.has_ctx && m.has_model && (m.ctx_t != m.model_t)) {
                    char c0[24];
                    char m0[24];

                    fmt_x100(c0, sizeof(c0), m.ctx_t * 100);
                    fmt_x100(m0, sizeof(m0), m.model_t * 100);
                    snprintf(frame_why, sizeof(frame_why), "alone-not-isolated-block(ctx=%s,model=%s)", c0, m0);
                    frame_dep = 1;
                }
                if ((m.has_ctx != m1.has_ctx) || (m.has_ctx && (m.ctx_t != m1.ctx_t)) || (m.has_wire != m1.has_wire) ||
                    (m.has_wire && (m.wc_t != m1.wc_t)))
                    frame_diff_count++;
                else if (m.has_ctx || m.has_wire)
                    frame_same_count++;
            } else
                measure_unit(unit, nunit, carrier, &pfx, single, lo, hi, div, path, run, cond, -1, &m);
            /* What compiled code charges beyond the model (off_model()),
               in the model's figure and in ctx's; a misaligned row's figure
               is the penalty alone (misaligned_penalty()). A prefix row's
               carrier charges the same with and without the prefix. */
            if (!strcmp(cond, "misaligned")) {
                misaligned_penalty(&unit[0], &om);
                m.model_t = 0;
                m.model   = 0;
            } else if (!carrier) {
                for (int i = 0; i < nunit; i++)
                    off_model_one(&unit[i], cond, &om);
            }
            if (drive_skipped)
                offm_no(&om, "pop-ss-mov-ss-op-runs-next-on-interpreter-charge");
            drive_real = 0;
            xm         = per_x100(om.c * (single ? 1 : (hi - lo)), m.mdiv);
            xc         = per_x100(om.c * (hi - lo), div);
            if (run) {
                if (!m.has_wire) {
                    snprintf(gap, sizeof(gap), "norun");
                    norun_count++;
                } else if (m.has_ctx && (m.wc == m.ctx + xc)) {
                    /* the compiled run is charged the model plus the terms
                       outside it; the scored figure differing from ctx's
                       (an isolated block's start, or a latency's or
                       throughput's steady state, choose_window()) is a
                       defect in itself */
                    if (!m.has_model || (m.model + xm != m.ctx + xc)) {
                        snprintf(gap, sizeof(gap), "=ctx");
                        model_ctx_count++;
                    } else
                        snprintf(gap, sizeof(gap), "=");
                } else if (m.cond_branch && m.has_ctx_alt && (m.wc == m.ctx_alt + xc))
                    snprintf(gap, sizeof(gap), "otherpath"); /* the flags an earlier copy left sent the run the other way */
                else {
                    snprintf(gap, sizeof(gap), "GAP");
                    gap_count++;
                }
            }
        }
        fmt_opt(model, sizeof(model), m.has_model, m.model, m.mwhy[0] ? m.mwhy : "fatal");
        fmt_opt(ctx, sizeof(ctx), m.has_ctx, m.ctx, "-");
        fmt_opt(wc, sizeof(wc), m.has_wire, m.wc, (run && m.why[0]) ? m.why : "-");
        fmt_opt(wi, sizeof(wi), m.has_wire, m.wi, (run && m.why[0]) ? m.why : "-");
    }

    if (is_rep && formula_slope(book, slope, sizeof(slope)))
        book = slope;
    if (!strcasecmp(status, "no-book") || !strcmp(r->f[C_BOOK], "-") || !r->f[C_BOOK][0])
        verdict = verdict_i = V_NO_BOOK;
    else if (!strncasecmp(status, "split", 5)) {
        verdict = verdict_i = V_SPLIT;
        snprintf(delta, sizeof(delta), "%s", status); /* every document's value */
    } else if (bad_dep) {
        verdict = verdict_i = bad_dep;
        why_un  = dep_why;
    }
    else if (rep_fixed)
        verdict = verdict_i = V_FORMULA;
    else if (!strcmp(cond, "miss"))
        verdict = verdict_i = V_NO_MODEL; /* a cache or TLB miss penalty: no model charges one */
    else {
        if (is_rep) {
            /* The element charge is the helpers' and the interpreter's,
               never the model's: the model column shows the hook charge. */
            if (m.has_wire)
                score_value(book, m.wc, sil, &verdict, delta, dir, sizeof(delta));
            else
                verdict = V_NORUN;
        } else {
            /* The figure a row is scored on is what the emulator charges:
               the compiled run's measured cycles where it ran on the row's
               path; else ctx (the model's hooks over the same stream) plus
               the charges outside the model (off_model()), or the isolated
               block's where no ctx was taken; else NOT-MEASURED, its reason
               in the delta. The model's own figure differing from it is
               the wire column's flag, never the verdict. */
            const char *src = NULL;
            int64_t     fig = 0;

            if (!strcmp(cond, "misaligned")) {
                if (!om.why[0] && !om.unread[0]) {
                    fig = xm;
                    src = "reproduced=penalty";
                }
            } else if (frame_dep)
                why_un = om.why[0] ? om.why : frame_why;
            else if (m.has_wire && m.cond_branch && strcmp(gap, "=") && strcmp(gap, "=ctx"))
                why_un = "branch-path-not-shown";
            else if (m.has_wire) {
                fig = m.wc;
                src = "measured=wire_c";
            } else if (om.why[0])
                why_un = om.why;
            else if (run_rejected(cond, &m))
                why_un = "run-rejected";
            else if (om.unread[0])
                why_un = om.unread;
            else if (m.has_ctx) {
                fig = m.ctx + xc;
                src = "reproduced=ctx+outside";
            } else if (m.has_model) {
                fig = m.model + xm;
                src = "reproduced=model+outside";
            } else
                why_un = m.mwhy[0] ? m.mwhy : "no-figure";
            if (src) {
                score_value(book, fig, sil, &verdict, delta, dir, sizeof(delta));
                meter_putf(delta, sizeof(delta), " %s", src);
                if (om.c) {
                    char mm[32];
                    char x[32];

                    fmt_x100(mm, sizeof(mm), m.model);
                    fmt_x100(x, sizeof(x), xm);
                    meter_putf(delta, sizeof(delta), " model=%s outside=%s(%s)", mm, x, om.names);
                }
            } else {
                verdict = V_OFFMODEL;
                if (!why_un)
                    why_un = om.why[0] ? om.why : (om.unread[0] ? om.unread : "no-figure");
            }
        }
        if (m.has_wire && m.cond_branch && strcmp(gap, "="))
            verdict_i = V_BRANCH; /* the run's path is not shown to be the row's */
        else if (m.has_wire)
            score_value(book, m.wi, sil, &verdict_i, delta_i, dir_i, sizeof(delta_i));
        else
            verdict_i = V_NORUN;
    }

print:
    if (why_un)
        snprintf(delta, sizeof(delta), "unscored:%s", why_un);
    if (nobook) {
        verdict = verdict_i = V_NO_BOOK;
        snprintf(delta, sizeof(delta), "-");
        snprintf(dir, sizeof(dir), "-");
    } else if (rider) {
        if (scored(verdict)) {
            char d[300];

            snprintf(d, sizeof(d), "%s:%s", r->f[C_PARTS][0] ? r->f[C_PARTS] : "-", delta);
            snprintf(delta, sizeof(delta), "%s", d);
            verdict = V_RIDER;
        }
        if (scored(verdict_i))
            verdict_i = V_RIDER;
    }
    if (is_rep)
        vc_count[verdict]++;
    else
        v_count[verdict]++;
    vi_count[verdict_i]++;
    printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", target,
           r->f[C_PARTS][0] ? r->f[C_PARTS] : "-", r->id, kind[0] ? kind : "-", r->f[C_MEANING][0] ? r->f[C_MEANING] : "-",
           cond[0] ? cond : "-", book[0] ? r->f[C_BOOK] : "-", model, ctx, wc, wi, v_names[verdict], v_names[verdict_i], delta,
           dir, gap, code_row, code_row_i, code_row_d);
}

/* ---- the run --------------------------------------------------------- */

/* A Pentium book's x87 row (the Pentium model; an x87 instruction or
   FWAIT, kind single, clocks): its one figure is the latency, timed on a
   dependent chain; "a/b" is latency a and throughput b (Pentium Vol. 3
   F.3.2), two rows, row_id /lat and /tput. */
static char m_latency[]    = "latency";
static char m_throughput[] = "throughput";

static int
pentium_fp(const row_t *r)
{
    ins_t in[UNIT_MAX];
    int   op;
    int   modrm;
    int   at;
    int   pfx;

    if (strcmp(model_name, "pentium") || strcmp(r->f[C_KIND], "single") || strcmp(r->f[C_MEANING], "clocks") ||
        (parse_units(r->f[C_BYTES], in, UNIT_MAX) != 1))
        return 0;
    return !x87_decode(&in[0], &op, &modrm, &at, &pfx) || ((in[0].len == 1) && (in[0].b[0] == 0x9b));
}

/* The '/' of an "a/b" figure, or NULL. */
static const char *
fp_slash(const char *book)
{
    const char *s = strchr(book, '/');
    int64_t     v;

    if (!s || !parse_x100(book, s, &v) || !parse_x100(s + 1, book + strlen(book), &v))
        return NULL;
    return s;
}

int
meter_run(const char *book, const char *aliases, int all_rows, int no_wire, int rider, int nobook, const char *family,
          const char *cpu_name)
{
    char  *text = read_all(book);
    char  *cells[64];
    int    map[NCOL];
    int    have_header = 0;
    row_t *rows        = NULL;
    int    nrows       = 0;
    int    cap         = 0;
    int    line        = 0;
    int    skipped     = 0;
    int    notes       = 0;
    int    extra       = 0; /* the /tput rows split from a/b figures */
    char   target[128];
    char  *save;

    if (!text) {
        perror(book);
        return 2;
    }
    if (frame_setup()) {
        fprintf(stderr, "meter: the harness's prelude or epilogue holds a form the meter does not split\n");
        return 2;
    }
    model_identify();
    snprintf(target, sizeof(target), "%s:%s", family, cpu_name);
    for (int i = 0; i < NCOL; i++)
        map[i] = -1;

    for (char *l = text; l; l = save) {
        int    n;
        size_t len;

        save = strchr(l, '\n');
        if (save)
            *save++ = 0;
        len = strlen(l);
        line++;
        if (len && (l[len - 1] == '\r'))
            l[--len] = 0;
        if (!len || (l[0] == '#'))
            continue;
        n = split_tabs(l, cells, 64);
        if (!have_header) {
            for (int c = 0; c < n; c++) {
                for (int k = 0; k < NCOL; k++) {
                    if (!strcasecmp(cells[c], col_names[k]))
                        map[k] = c;
                }
            }
            if (map[C_BYTES] < 0) {
                fprintf(stderr, "%s: the header line names no bytes column\n", book);
                return 2;
            }
            have_header = 1;
            continue;
        }
        if (nrows + 2 > cap) {
            cap  = cap ? cap * 2 : 256;
            rows = realloc(rows, cap * sizeof(row_t));
            if (!rows)
                harness_fatal("no memory for the book");
        }
        {
            row_t *r = &rows[nrows];

            for (int k = 0; k < NCOL; k++) {
                r->f[k] = ((map[k] >= 0) && (map[k] < n)) ? cells[map[k]] : empty;
                while (isspace((unsigned char) *r->f[k]))
                    r->f[k]++;
                for (char *e = r->f[k] + strlen(r->f[k]); (e > r->f[k]) && isspace((unsigned char) e[-1]); e--)
                    e[-1] = 0;
            }
            if (!all_rows && !token_match(r->f[C_PARTS], family, target, cpu_s->name, aliases)) {
                skipped++;
                continue;
            }
            if (!is_measured_meaning(r->f[C_MEANING])) {
                notes++;
                continue;
            }
            r->line = line;
            if (r->f[C_ROWID][0])
                r->id = strdup(r->f[C_ROWID]);
            else {
                size_t sz = strlen(r->f[C_BYTES]) + strlen(r->f[C_KIND]) + strlen(r->f[C_COND]) + strlen(r->f[C_MEANING]) + 8;

                r->id = malloc(sz);
                snprintf(r->id, sz, "%s|%s|%s|%s", r->f[C_BYTES], r->f[C_KIND], r->f[C_COND], r->f[C_MEANING]);
                for (char *p = r->id; *p; p++) {
                    if (*p == ' ')
                        *p = '.';
                }
            }
            r->own  = NULL;
            r->fp   = FP_NONE;
            r->fp_a = r->fp_b = NULL;
            r->fx   = 0;
            if (pentium_fp(r)) {
                const char *slash = fp_slash(r->f[C_BOOK]);

                r->f[C_MEANING] = m_latency;
                r->fp           = FP_ONE;
                if (slash) {
                    row_t *t  = &rows[nrows + 1];
                    size_t sz = strlen(r->id) + 8;

                    r->own                     = strdup(r->f[C_BOOK]);
                    r->own[slash - r->f[C_BOOK]] = 0;
                    *t                         = *r;
                    t->own                     = NULL;
                    t->f[C_MEANING]            = m_throughput;
                    t->f[C_BOOK]               = r->own + (slash - r->f[C_BOOK]) + 1;
                    t->id                      = malloc(sz);
                    snprintf(t->id, sz, "%s/tput", r->id);
                    r->id = realloc(r->id, sz);
                    strcat(r->id, "/lat");
                    r->f[C_BOOK] = r->own;
                    r->fp        = FP_LAT;
                    t->fp        = FP_TPUT;
                    r->fp_a = t->fp_a = r->own;
                    r->fp_b = t->fp_b = t->f[C_BOOK];
                    nrows++;
                    extra++;
                }
            }
            nrows++;
        }
    }
    /* Whether the book pairs an x87 row's instruction with FXCH: its
       pairing row, the same bytes for the same parts, reads FX. */
    for (int i = 0; i < nrows; i++) {
        if (rows[i].fp != FP_TPUT)
            continue;
        for (int j = 0; j < nrows; j++) {
            if (!strcmp(rows[j].f[C_MEANING], "pairing") && !strcmp(rows[j].f[C_BYTES], rows[i].f[C_BYTES]) &&
                !strcmp(rows[j].f[C_PARTS], rows[i].f[C_PARTS]) && !strcasecmp(rows[j].f[C_BOOK], "FX"))
                rows[i].fx = 1;
        }
    }
    if (!have_header) {
        fprintf(stderr, "%s: no header line\n", book);
        return 2;
    }
    qsort(rows, nrows, sizeof(row_t), row_cmp);

    printf("# meter target %s (%s) model %s fpu %s%s book rows %d measured %d other parts %d notes %d\n", target, cpu_s->name,
           model_name, fpu_softfloat ? "softfloat" : "x87", nobook ? " nobook" : rider ? " rider" : "",
           nrows - extra + skipped + notes, nrows, skipped, notes);
    meter_dump_vars();
    meter_dump_x87();
    printf("# part\tparts\trow_id\tkind\tmeaning\tcond\tbook\tmodel\tctx\twire_c\twire_i\tverdict\tverdict_i\tdelta\tdir\twire\t"
           "code_row\tcode_row_i\tcode_row_d\n");
    for (int i = 0; i < nrows; i++)
        meter_row(&rows[i], target, no_wire, rider, nobook);
    for (int v = 0; v < V_NR; v++)
        printf("COUNT\t%s\t%s\t%d\n", target, v_names[v], v_count[v]);
    for (int v = 0; v < V_NR; v++)
        printf("COUNT\t%s\tI-%s\t%d\n", target, v_names[v], vi_count[v]);
    for (int v = 0; v < V_NR; v++)
        printf("COUNT\t%s\tC-%s\t%d\n", target, v_names[v], vc_count[v]);
    printf("COUNT\t%s\tGAP\t%d\n", target, gap_count);
    printf("COUNT\t%s\tWIRE-MODEL-CTX\t%d\n", target, model_ctx_count);
    printf("COUNT\t%s\tSEED-MM0\t%d\n", target, seed_mm0_count);
    printf("COUNT\t%s\tWIRE-NORUN\t%d\n", target, norun_count);
    printf("COUNT\t%s\tWIRE-RUNS\t%d\n", target, wire_run_count);
    printf("COUNT\t%s\tFRAME-SAME\t%d\n", target, frame_same_count);
    printf("COUNT\t%s\tFRAME-DIFF\t%d\n", target, frame_diff_count);
    for (int i = 0; i < nrows; i++) {
        free(rows[i].id);
        free(rows[i].own);
    }
    free(rows);
    free(text);
    return 0;
}
