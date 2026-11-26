/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: reads the WinChip 2 timing model's
 *          tables (clocks and pairing per opcode). The tables are static
 *          inside codegen_timing_winchip2.c, so this file compiles that
 *          file a second time with its public names renamed, then reads the
 *          tables from that copy; the copy's functions are never called.
 */
#define codegen_timing_winchip2 meter_copy_winchip2
#define can_pair                meter_copy_winchip2_can_pair
#include "codegen_timing_winchip2.c"

#include "meter.h"

#define T(s) opcode_timings_winchip2##s
#define METER_WITH_DEPS

int
meter_struct_winchip2(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, UNUSED(uint32_t op_pc), UNUSED(int nprefix),
                      char *out, size_t n)
{
    uint32_t       *timings;
    const char     *name;
    const uint64_t *deps;
    const char     *dname;
    int             mod3 = ((fetchdat & 0xc0) == 0xc0);
    uint32_t        t;

#include "meter_pick.h"
    t = timings[opcode];
    meter_putf(out, n, "row=%s[%02x];deps=%s[%02x];depmask=%016" PRIx64 ";clocks=%d", name, opcode, dname, opcode, deps[opcode],
               COUNT(t, op_32));
    if (t & CYCLES_FPU)
        meter_putf(out, n, ";fpu=%u/%u/%u", FPU_COMP_TIME(t), FPU_I_OVERLAP(t), FPU_F_OVERLAP(t));
    if (t & CYCLES_IS_MMX)
        meter_putf(out, n, ";mmx=%s", (t & CYCLES_IS_MMX_MUL) ? "mul" : (t & CYCLES_IS_MMX_SHIFT) ? "shift" : (t & CYCLES_IS_3DNOW) ? "3dnow" : "any");
    return 1;
}
