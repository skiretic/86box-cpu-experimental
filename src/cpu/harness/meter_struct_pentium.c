/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: reads the Pentium timing model's tables
 *          (clocks and pairing class per opcode; the tables are named p6 in
 *          that file). The tables are static inside
 *          codegen_timing_pentium.c, so this file compiles that file a
 *          second time with its public functions renamed, then reads the
 *          tables from that copy; the copy's functions are never called.
 */
#define codegen_timing_pentium             meter_copy_pentium
#define codegen_timing_pentium_start       meter_copy_pentium_start
#define codegen_timing_pentium_prefix      meter_copy_pentium_prefix
#define codegen_timing_pentium_opcode      meter_copy_pentium_opcode
#define codegen_timing_pentium_block_start meter_copy_pentium_block_start
#define codegen_timing_pentium_block_end   meter_copy_pentium_block_end
#include "codegen_timing_pentium.c"

#include "meter.h"

#define T(s) opcode_timings_p6##s
#define METER_WITH_DEPS
#define METER_SHIFT_PENTIUM

int
meter_struct_pentium(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, UNUSED(uint32_t op_pc), UNUSED(int nprefix),
                     char *out, size_t n)
{
    uint64_t       *timings;
    const char     *name;
    const uint64_t *deps;
    const char     *dname;
    int             mod3 = ((fetchdat & 0xc0) == 0xc0);
    uint64_t        t;
    const char     *pair;

#include "meter_pick.h"
    t = timings[opcode];
    meter_putf(out, n, "row=%s[%02x];deps=%s[%02x];depmask=%016" PRIx64, name, opcode, dname, opcode, deps[opcode]);
    /* An empty (INVALID) entry runs as an instruction that pairs with
       nothing, for COUNT()'s 0 clocks. */
    if (!t) {
        meter_putf(out, n, ";invalid=1;pair=NP;clocks=0");
        return 1;
    }
    switch (t & PAIR_MASK) {
        case PAIR_U:
            pair = "U";
            break;
        case PAIR_V:
            pair = "V";
            break;
        case PAIR_UV:
            pair = "UV";
            break;
        case PAIR_FX:
            pair = "FX";
            break;
        case PAIR_FXCH:
            pair = "FXCH";
            break;
        case PAIR_FPU:
            pair = "FPU";
            break;
        default:
            pair = "NP";
            break;
    }
    meter_putf(out, n, ";pair=%s;clocks=%d", pair, COUNT(t, deps[opcode], op_32));
    if ((t & PAIR_FPU) && !(t & CYCLES_HAS_MULTI))
        meter_putf(out, n, ";fpu=%u/%u/%u", (unsigned) FPU_COMP_TIME(t), (unsigned) FPU_I_OVERLAP(t), (unsigned) FPU_F_OVERLAP(t));
    if (t & CYCLES_HAS_MULTI)
        meter_putf(out, n, ";multi=%u/%u", (unsigned) (t & 0xff), (unsigned) ((t >> 8) & 0xff));
    return 1;
}
