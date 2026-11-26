/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: reads the WinChip timing model's tables
 *          (clocks per opcode). The tables are static inside
 *          codegen_timing_winchip.c, so this file compiles that file a
 *          second time with its public functions renamed, then reads the
 *          tables from that copy; the copy's functions are never called.
 *          The WinChip 2 model is read by meter_struct_winchip2.c: the two
 *          model files use the same names for their static tables, so they
 *          cannot be compiled into one file.
 */
#define codegen_timing_winchip             meter_copy_winchip
#define codegen_timing_winchip_start       meter_copy_winchip_start
#define codegen_timing_winchip_prefix      meter_copy_winchip_prefix
#define codegen_timing_winchip_opcode      meter_copy_winchip_opcode
#define codegen_timing_winchip_block_start meter_copy_winchip_block_start
#define codegen_timing_winchip_block_end   meter_copy_winchip_block_end
#include "codegen_timing_winchip.c"

#include "meter.h"

#define T(s) opcode_timings_winchip##s
#define METER_WITH_DEPS

int
meter_struct_winchip(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, UNUSED(uint32_t op_pc), UNUSED(int nprefix),
                     char *out, size_t n)
{
    int       **timings;
    const char     *name;
    const uint64_t *deps;
    const char     *dname;
    int             mod3 = ((fetchdat & 0xc0) == 0xc0);

#include "meter_pick.h"
    meter_putf(out, n, "row=%s[%02x];deps=%s[%02x];depmask=%016" PRIx64 ";clocks=%d", name, opcode, dname, opcode,
               deps[opcode], timings[opcode] ? COUNT(timings[opcode], op_32) : 0);
    if (!timings[opcode])
        meter_putf(out, n, ";null=1");
    return 1;
}
