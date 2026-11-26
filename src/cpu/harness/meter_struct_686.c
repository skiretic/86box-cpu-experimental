/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: reads the Cyrix 6x86 timing model's
 *          tables (clocks and pairing class per opcode). The tables are
 *          static inside codegen_timing_686.c, so this file compiles that
 *          file a second time with its public functions renamed, then reads
 *          the tables from that copy; the copy's functions are never
 *          called.
 */
#define codegen_timing_686             meter_copy_686
#define codegen_timing_686_start       meter_copy_686_start
#define codegen_timing_686_prefix      meter_copy_686_prefix
#define codegen_timing_686_opcode      meter_copy_686_opcode
#define codegen_timing_686_block_start meter_copy_686_block_start
#define codegen_timing_686_block_end   meter_copy_686_block_end
#include "codegen_timing_686.c"

#include "meter.h"

#define T(s) opcode_timings_686##s
#define METER_SHIFT_686
#define METER_WITH_DEPS

int
meter_struct_686(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, UNUSED(uint32_t op_pc), UNUSED(int nprefix),
                 char *out, size_t n)
{
    uint32_t       *timings;
    const char     *name;
    const uint64_t *deps;
    const char     *dname;
    int             mod3 = ((fetchdat & 0xc0) == 0xc0);
    uint32_t        t;
    const char     *pair;

#include "meter_pick.h"
    t = timings[opcode];
    meter_putf(out, n, "row=%s[%02x];deps=%s[%02x];depmask=%016" PRIx64, name, opcode, dname, opcode, deps[opcode]);
    /* An empty (INVALID) entry runs as an instruction that pairs with
       nothing (PAIR_NP is 0), for COUNT()'s 0 clocks. */
    if (!t) {
        meter_putf(out, n, ";invalid=1;pair=NP;clocks=0");
        return 1;
    }
    switch (t & PAIR_MASK) {
        case PAIR_X:
            pair = "X";
            break;
        case PAIR_X_BRANCH:
            pair = "XBRANCH";
            break;
        case PAIR_XY:
            pair = "XY";
            break;
        default:
            pair = "NP";
            break;
    }
    meter_putf(out, n, ";pair=%s;clocks=%d", pair, COUNT(t, op_32));
    if (t & CYCLES_HAS_MULTI)
        meter_putf(out, n, ";multi=%u/%u", t & 0xff, (t >> 8) & 0xff);
    return 1;
}
