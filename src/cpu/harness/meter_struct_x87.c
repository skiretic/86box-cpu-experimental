/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: the x87 timing tables cpu_set() chose
 *          (the interpreter's x87 charges) and the timing_* variables.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include "cpu.h"
#include "x87_timings.h"

#include "meter.h"

void
meter_putf(char *out, size_t n, const char *fmt, ...)
{
    size_t  len = strlen(out);
    va_list ap;

    if (len + 1 >= n)
        return;
    va_start(ap, fmt);
    vsnprintf(out + len, n - len, fmt, ap);
    va_end(ap);
}

#define X87(f) { #f, offsetof(x87_timings_t, f) }
static const struct {
    const char *name;
    size_t      off;
} x87_fields[] = {
    X87(f2xm1), X87(fabs), X87(fadd), X87(fadd_32), X87(fadd_64), X87(fbld), X87(fbstp), X87(fchs), X87(fclex),
    X87(fcom), X87(fcom_32), X87(fcom_64), X87(fcos), X87(fincdecstp), X87(fdisi_eni), X87(fdiv), X87(fdiv_32),
    X87(fdiv_64), X87(ffree), X87(fadd_i16), X87(fadd_i32), X87(fcom_i16), X87(fcom_i32), X87(fdiv_i16),
    X87(fdiv_i32), X87(fild_16), X87(fild_32), X87(fild_64), X87(fmul_i16), X87(fmul_i32), X87(finit),
    X87(fist_16), X87(fist_32), X87(fist_64), X87(fld), X87(fld_32), X87(fld_64), X87(fld_80), X87(fld_z1),
    X87(fld_const), X87(fldcw), X87(fldenv), X87(fmul), X87(fmul_32), X87(fmul_64), X87(fnop), X87(fpatan),
    X87(fprem), X87(fprem1), X87(fptan), X87(frndint), X87(frstor), X87(fsave), X87(fscale), X87(fsetpm),
    X87(fsin_cos), X87(fsincos), X87(fsqrt), X87(fst), X87(fst_32), X87(fst_64), X87(fst_80), X87(fstcw_sw),
    X87(fstenv), X87(ftst), X87(fucom), X87(fwait), X87(fxam), X87(fxch), X87(fxtract), X87(fyl2x), X87(fyl2xp1)
};
#undef X87

const char *
meter_x87_table_name(void)
{
    static const struct {
        const x87_timings_t *t;
        const char          *name;
    } known[] = {
        { &x87_timings_8087, "x87_timings_8087" },
        { &x87_timings_80187, "x87_timings_80187" },
        { &x87_timings_287, "x87_timings_287" },
        { &x87_timings_387, "x87_timings_387" },
        { &x87_timings_486, "x87_timings_486" }
    };

    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (!memcmp(&x87_timings, known[i].t, sizeof(x87_timings)))
            return known[i].name;
    }
    return "other";
}

/* The live tables are copies, so they are named by the constant table they
   equal. */
void
meter_dump_x87(void)
{
    printf("X87\ttable\t%s\n", meter_x87_table_name());
    printf("X87\tconcurrency\t%s\n", !memcmp(&x87_concurrency, &x87_concurrency_486, sizeof(x87_concurrency)) ? "x87_concurrency_486" : "other");
    for (size_t i = 0; i < sizeof(x87_fields) / sizeof(x87_fields[0]); i++)
        printf("X87\t%s\t%d\t%d\n", x87_fields[i].name, *(const int *) ((const uint8_t *) &x87_timings + x87_fields[i].off),
               *(const int *) ((const uint8_t *) &x87_concurrency + x87_fields[i].off));
}

#define VAR(v) { #v, &v }
void
meter_dump_vars(void)
{
    const struct {
        const char *name;
        const int  *v;
    } vars[] = {
        VAR(timing_rr), VAR(timing_mr), VAR(timing_mrl), VAR(timing_rm), VAR(timing_rml), VAR(timing_mm),
        VAR(timing_mml), VAR(timing_bt), VAR(timing_bnt), VAR(timing_int), VAR(timing_int_rm), VAR(timing_int_v86),
        VAR(timing_int_pm), VAR(timing_int_pm_outer), VAR(timing_iret_rm), VAR(timing_iret_v86), VAR(timing_iret_pm),
        VAR(timing_iret_pm_outer), VAR(timing_call_rm), VAR(timing_call_pm), VAR(timing_call_pm_gate),
        VAR(timing_call_pm_gate_inner), VAR(timing_retf_rm), VAR(timing_retf_pm), VAR(timing_retf_pm_outer),
        VAR(timing_jmp_rm), VAR(timing_jmp_pm), VAR(timing_jmp_pm_gate), VAR(timing_misaligned)
    };

    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++)
        printf("VAR\t%s\t%d\n", vars[i].name, *vars[i].v);
}
#undef VAR
