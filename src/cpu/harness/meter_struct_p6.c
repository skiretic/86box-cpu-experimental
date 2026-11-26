/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: reads the P6 timing model's tables (uop
 *          count and decoder class per opcode). The tables are static
 *          inside codegen_timing_p6.c, so this file compiles that file a
 *          second time with its public functions renamed, then reads the
 *          tables from that copy; the copy's functions are never called.
 *          decoder is the table's class with decode_instruction()'s rule
 *          applied: a simple decoder takes an instruction of one uop and at
 *          most 7 bytes, its length counted as the model counts it
 *          (codegen_timing_instr_length(), the prefixes from the hook's
 *          prefix count, 0F included).
 */
#define codegen_timing_p6             meter_copy_p6
#define codegen_timing_p6_start       meter_copy_p6_start
#define codegen_timing_p6_prefix      meter_copy_p6_prefix
#define codegen_timing_p6_opcode      meter_copy_p6_opcode
#define codegen_timing_p6_block_start meter_copy_p6_block_start
#define codegen_timing_p6_block_end   meter_copy_p6_block_end
#define codegen_timing_p6_jump_cycles meter_copy_p6_jump_cycles
#define decode_flush_p6               meter_copy_decode_flush_p6
#include "codegen_timing_p6.c"

#include "meter.h"

#define T(s) opcode_timings_p6##s
#define METER_GROUPS_P6
#define METER_WITH_DEPS

static const char *p6_uop_names[] = {
    "ALU", "ALUP0", "LOAD", "STORED", "STOREA", "FLOAD", "FSTORED", "FSTOREA", "MLOAD", "MSTORED", "MSTOREA",
    "FLOAT", "MMX", "MMX_SHIFT", "MMX_MUL", "BRANCH", "FXCH"
};

int
meter_struct_p6(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, UNUSED(uint32_t op_pc), int nprefix,
                char *out, size_t n)
{
    const macro_op_t **timings;
    const uint64_t    *deps;
    const char        *dname;
    const char        *name;
    int                mod3 = ((fetchdat & 0xc0) == 0xc0);
    const macro_op_t  *ins;
    uint64_t           dep;
    int                fallback = 0;
    int                len;

#include "meter_pick.h"
    meter_putf(out, n, "row=%s[%02x];deps=%s[%02x];depmask=%016" PRIx64, name, opcode, dname, opcode, deps[opcode]);
    if ((last_prefix == 0x0f) && !cpu_has_feature(CPU_FEATURE_MMX) && (((opcode & 0xe0) == 0x60) || (opcode >= 0xd0))) {
        ins      = &complex_alu1_op;
        dep      = 0;
        fallback = 1;
    } else if (timings[opcode]) {
        ins = timings[opcode];
        dep = deps[opcode];
    } else {
        ins      = &complex_alu1_op;
        dep      = 0;
        fallback = 2;
    }
    /* decode_instruction()'s rule: a simple decoder takes one uop and at
       most 7 bytes, by the model's own length count (prefixes from the
       hook's prefix count, 0F included). */
    prefixes = nprefix;
    len      = codegen_timing_instr_length(dep, fetchdat, op_32);
    prefixes = 0;
    meter_putf(out, n, ";uops=%d;decoder=%s;table_decoder=%s;len=%d;units=", ins->nr_uops,
               ((ins->decode_type == DECODE_SIMPLE) && (ins->nr_uops <= 1) && (len <= 7)) ? "simple" : "complex",
               (ins->decode_type == DECODE_SIMPLE) ? "simple" : "complex", len);
    for (int c = 0; c < ins->nr_uops; c++)
        meter_putf(out, n, "%s%s", c ? "," : "", p6_uop_names[ins->uop[c].type]);
    meter_putf(out, n, ";lat=");
    for (int c = 0; c < ins->nr_uops; c++)
        meter_putf(out, n, "%s%d", c ? "," : "", ins->uop[c].latency);
    /* ports: the units of the model's own table for this CPU (start() picks
       it the same way) that take any of the uops, by the books' port
       numbers: the table's index 3 takes the store data uops, which the
       books number port 4, and its index 4 the store address uops, port 3. */
    {
        const p6_unit_t *u  = (cpu_s->cpu_type == CPU_PENTIUMPRO) ? ppro_units : p2_units;
        const int        nu = (cpu_s->cpu_type == CPU_PENTIUMPRO) ? (int) NR_PPRO_UNITS : (int) NR_P2_UNITS;
        int              any = 0;

        meter_putf(out, n, ";ports=");
        for (int p = 0; p < nu; p++) {
            for (int c = 0; c < ins->nr_uops; c++) {
                if (u[p].uop_mask & (1u << ins->uop[c].type)) {
                    meter_putf(out, n, "%s%d", any ? "," : "", (p == 3) ? 4 : (p == 4) ? 3 : p);
                    any = 1;
                    break;
                }
            }
        }
        if (!any)
            meter_putf(out, n, "none");
    }
    if (fallback)
        meter_putf(out, n, ";fallback=%s", (fallback == 1) ? "nommx" : "null");
    return 1;
}
