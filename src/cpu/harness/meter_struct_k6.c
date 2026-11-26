/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: reads the K6 timing model's tables
 *          (RISC86 op counts, decode type). The tables are static inside
 *          codegen_timing_k6.c, so this file compiles that file a second
 *          time with its public functions renamed, then reads the tables
 *          from that copy; the copy's functions are never called. A 3DNow!
 *          instruction's table index is its suffix byte, read from guest
 *          memory where the model's opcode hook reads it.
 */
#define codegen_timing_k6             meter_copy_k6
#define codegen_timing_k6_start       meter_copy_k6_start
#define codegen_timing_k6_prefix      meter_copy_k6_prefix
#define codegen_timing_k6_opcode      meter_copy_k6_opcode
#define codegen_timing_k6_block_start meter_copy_k6_block_start
#define codegen_timing_k6_block_end   meter_copy_k6_block_end
#define codegen_timing_k6_jump_cycles meter_copy_k6_jump_cycles
#define decode_flush_k6               meter_copy_decode_flush_k6
#include "codegen_timing_k6.c"

#include "meter.h"

#define T(s) opcode_timings_k6##s
#define METER_GROUPS_P6
#define METER_WITH_DEPS
#define METER_HAS_0F0F
#define METER_0F0F_SUFFIX k6_suffix(fetchdat, op_32, op_pc)

static const char *k6_uop_names[] = {
    "ALU", "ALUX", "LOAD", "STORE", "FLOAD", "FSTORE", "MLOAD", "MSTORE", "FLOAT", "MEU", "MEU_SHIFT", "MEU_MUL",
    "MEU_3DN", "BRANCH", "LIMM"
};
static const char *k6_decode_names[] = { "short", "long", "vector" };

/* Where codegen_timing_k6_opcode finds the suffix: past the ModR/M, the
   SIB and the displacement. */
static uint8_t
k6_suffix(uint32_t fetchdat, int op_32, uint32_t op_pc)
{
    uint32_t pc    = op_pc + 1;
    uint8_t  modrm = fetchdat & 0xff;
    uint8_t  sib   = (fetchdat >> 8) & 0xff;

    if ((modrm & 0xc0) != 0xc0) {
        if (op_32 & 0x200) {
            if ((modrm & 7) == 4) {
                pc++;
                if ((modrm & 0xc0) == 0x40)
                    pc++;
                else if ((modrm & 0xc0) == 0x80)
                    pc += 4;
                else if ((sib & 0x07) == 0x05)
                    pc += 4;
            } else {
                if ((modrm & 0xc0) == 0x40)
                    pc++;
                else if ((modrm & 0xc0) == 0x80)
                    pc += 4;
                else if ((modrm & 0xc7) == 0x05)
                    pc += 4;
            }
        } else {
            if ((modrm & 0xc0) == 0x40)
                pc++;
            else if ((modrm & 0xc0) == 0x80)
                pc += 2;
            else if ((modrm & 0xc7) == 0x06)
                pc += 2;
        }
    }
    return fastreadb(cs + pc);
}

int
meter_struct_k6(uint8_t last_prefix, uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc, int nprefix, char *out,
                size_t n)
{
    const risc86_instruction_t **timings;
    const uint64_t              *deps;
    const char                  *dname;
    const char                  *name;
    int                          mod3 = ((fetchdat & 0xc0) == 0xc0);
    const risc86_instruction_t  *ins;
    uint64_t                     dep      = 0;
    int                          fallback = 0;
    int                          len;
    decode_type_t                decode;

    if ((last_prefix == 0x0f) && (opcode == 0x0f) && !cpu_has_feature(CPU_FEATURE_3DNOW)) {
        name     = "no-3dnow";
        ins      = &vector_alu1_op;
        fallback = 1;
        meter_putf(out, n, "row=%s[0f]", name);
    } else {
#include "meter_pick.h"
        meter_putf(out, n, "row=%s[%02x];deps=%s[%02x];depmask=%016" PRIx64, name, opcode, dname, opcode, deps[opcode]);
        if (timings[opcode]) {
            ins = timings[opcode];
            dep = deps[opcode];
        } else {
            ins      = &vector_alu1_op;
            fallback = 2;
        }
    }
    /* decode_instruction()'s rule: a short decoder takes at most 7 bytes,
       the long decoder 11, by the model's own length count. */
    prefixes = nprefix;
    len      = codegen_timing_instr_length(dep, fetchdat, op_32);
    prefixes = 0;
    decode   = ins->decode_type;
    if ((decode == DECODE_SHORT) && (len > 7))
        decode = DECODE_LONG;
    else if (len > 11)
        decode = DECODE_VECTOR;
    meter_putf(out, n, ";uops=%d;decoder=%s;table_decoder=%s;len=%d;units=", ins->nr_uops, k6_decode_names[decode],
               k6_decode_names[ins->decode_type], len);
    for (int c = 0; c < ins->nr_uops; c++)
        meter_putf(out, n, "%s%s", c ? "," : "", k6_uop_names[ins->uop[c].type]);
    meter_putf(out, n, ";tput=");
    for (int c = 0; c < ins->nr_uops; c++)
        meter_putf(out, n, "%s%d", c ? "," : "", ins->uop[c].throughput);
    meter_putf(out, n, ";lat=");
    for (int c = 0; c < ins->nr_uops; c++)
        meter_putf(out, n, "%s%d", c ? "," : "", ins->uop[c].latency);
    if (fallback)
        meter_putf(out, n, ";fallback=%s", (fallback == 1) ? "no3dnow" : "null");
    return 1;
}
