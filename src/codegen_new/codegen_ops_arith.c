#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_arith.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"

static inline void
get_cf(ir_data_t *ir, int dest_reg)
{
    uop_CALL_FUNC_RESULT_ORDERED(ir, dest_reg, jit_CF_SET);
}

uint32_t
ropADC_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc);
        uop_ADD(ir, IREG_AL, IREG_AL, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_ADD(ir, IREG_AL, IREG_AL, IREG_temp0_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1_W, cs + op_pc);
        uop_ADD(ir, IREG_AX, IREG_AX, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_ADD(ir, IREG_AX, IREG_AX, IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropADC_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc);
        uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp1);
        uop_MOV(ir, IREG_flags_op2, IREG_temp1);
    } else {
        uint32_t imm_data = fastreadl(cs + op_pc);

        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp0);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropADC_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp2_B, IREG_temp0_B, IREG_8(src_reg));
        uop_ADD(ir, IREG_temp2_B, IREG_temp2_B, IREG_temp1_B);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp2_W, IREG_temp0_W, IREG_16(src_reg));
        uop_ADD(ir, IREG_temp2_W, IREG_temp2_W, IREG_temp1_W);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_W);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADC_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp2, IREG_temp0, IREG_32(src_reg));
        uop_ADD(ir, IREG_temp2, IREG_temp2, IREG_temp1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropADD_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc);
        uop_ADD(ir, IREG_AL, IREG_AL, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc);
        uop_ADD(ir, IREG_AX, IREG_AX, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_ADD_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropADD_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);

    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
        uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    } else {
        fetchdat = fastreadl(cs + op_pc);
        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        uop_MOV_IMM(ir, IREG_flags_op2, fetchdat);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);
    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropADD_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp1_B, IREG_temp0_B, IREG_8(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp1_W, IREG_temp0_W, IREG_16(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropADD_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_32(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropCMP_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc);
        uop_SUB(ir, IREG_flags_res_B, IREG_AL, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_SUB_IMM(ir, IREG_flags_res_B, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc);
        uop_SUB(ir, IREG_flags_res_W, IREG_AX, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_SUB_IMM(ir, IREG_flags_res_W, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropCMP_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
        uop_SUB(ir, IREG_flags_res, IREG_EAX, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    } else {
        uint32_t imm_data = fastreadl(cs + op_pc);

        uop_SUB_IMM(ir, IREG_flags_res, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropCMP_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_flags_res_B, IREG_temp0_B, IREG_8(src_reg));
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_flags_res_W, IREG_temp0_W, IREG_16(src_reg));
    }

    uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
        uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_temp0);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropCMP_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_flags_res, IREG_temp0, IREG_32(src_reg));
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropSBB_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc);
        uop_SUB(ir, IREG_AL, IREG_AL, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_SUB(ir, IREG_AL, IREG_AL, IREG_temp0_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1_W, cs + op_pc);
        uop_SUB(ir, IREG_AX, IREG_AX, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp1_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_SUB(ir, IREG_AX, IREG_AX, IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropSBB_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    get_cf(ir, IREG_temp0);
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc);
        uop_SUB(ir, IREG_EAX, IREG_EAX, IREG_temp1);
        uop_MOV(ir, IREG_flags_op2, IREG_temp1);
    } else {
        uint32_t imm_data = fastreadl(cs + op_pc);

        uop_SUB_IMM(ir, IREG_EAX, IREG_EAX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 4);
    }
    uop_SUB(ir, IREG_EAX, IREG_EAX, IREG_temp0);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropSBB_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp2_B, IREG_temp0_B, IREG_8(src_reg));
        uop_SUB(ir, IREG_temp2_B, IREG_temp2_B, IREG_temp1_B);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp2_W, IREG_temp0_W, IREG_16(src_reg));
        uop_SUB(ir, IREG_temp2_W, IREG_temp2_W, IREG_temp1_W);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp2_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSBB_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    get_cf(ir, IREG_temp1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp2, IREG_temp0, IREG_32(src_reg));
        uop_SUB(ir, IREG_temp2, IREG_temp2, IREG_temp1);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp2);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp2);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropSUB_AL_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AL);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc);
        uop_SUB(ir, IREG_AL, IREG_AL, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
    } else {
        uint8_t imm_data = fastreadb(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AL, IREG_AL, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 1);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
    uop_MOVZX(ir, IREG_flags_res, IREG_AL);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_AX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOVZX(ir, IREG_flags_op1, IREG_AX);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc);
        uop_SUB(ir, IREG_AX, IREG_AX, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    } else {
        uint16_t imm_data = fastreadw(cs + op_pc);

        uop_SUB_IMM(ir, IREG_AX, IREG_AX, imm_data);
        uop_MOV_IMM(ir, IREG_flags_op2, imm_data);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
    uop_MOVZX(ir, IREG_flags_res, IREG_AX);

    codegen_flags_changed = 1;
    return op_pc + 2;
}
uint32_t
ropSUB_EAX_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_MOV(ir, IREG_flags_op1, IREG_EAX);

    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc);
        uop_SUB(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    } else {
        fetchdat = fastreadl(cs + op_pc);
        codegen_mark_code_present(block, cs + op_pc, 4);
        uop_SUB_IMM(ir, IREG_EAX, IREG_EAX, fetchdat);
        uop_MOV_IMM(ir, IREG_flags_op2, fetchdat);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
    uop_MOV(ir, IREG_flags_res, IREG_EAX);

    codegen_flags_changed = 1;
    return op_pc + 4;
}
uint32_t
ropSUB_b_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
    uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_b_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp1_B, IREG_temp0_B, IREG_8(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_op2, IREG_8(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
    }
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_w_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp1_W, IREG_temp0_W, IREG_16(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_flags_op2, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_flags_op2);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
    uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropSUB_l_rmw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);

        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SUB(ir, IREG_temp1, IREG_temp0, IREG_32(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
        uop_MOV(ir, IREG_flags_op1, IREG_temp0);
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
        uop_MOV(ir, IREG_flags_res, IREG_temp1);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
rop80(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int skip_immediate = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int     dest_reg = fetchdat & 7;
        uint8_t imm      = fastreadb(cs + op_pc + 1);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0_B, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_ADD_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                else
                    uop_OR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_ADD_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_ADD(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_SUB_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp1_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                else
                    uop_AND_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_SUB_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_8(dest_reg), IREG_8(dest_reg), IREG_temp0_B);
                else
                    uop_XOR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_flags_res_B, IREG_8(dest_reg), IREG_temp0_B);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_B);
                } else {
                    uop_SUB_IMM(ir, IREG_flags_res_B, IREG_8(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                break;

            default:
                return 0;
        }
    } else {
        x86seg *target_seg;
        uint8_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        imm = fastreadb(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_ADD_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x08: /*OR*/
                uop_OR_IMM(ir, IREG_temp0_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp2);
                uop_ADD_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_ADD(ir, IREG_temp1_B, IREG_temp1_B, IREG_temp2_B);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp2);
                uop_SUB_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_SUB(ir, IREG_temp1_B, IREG_temp1_B, IREG_temp2_B);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x20: /*AND*/
                uop_AND_IMM(ir, IREG_temp0_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                break;

            case 0x28: /*SUB*/
                uop_SUB_IMM(ir, IREG_temp1_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x30: /*XOR*/
                uop_XOR_IMM(ir, IREG_temp0_B, IREG_temp0_B, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN8);
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_SUB_IMM(ir, IREG_flags_res_B, IREG_temp0_B, imm);
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    if (!skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
rop81_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int skip_immediate = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int      dest_reg = fetchdat & 7;
        uint16_t imm      = fastreadw(cs + op_pc + 1);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0_W, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_ADD_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                else
                    uop_OR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_ADD_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_SUB_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                else
                    uop_AND_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_SUB_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp0_W);
                else
                    uop_XOR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_flags_res_W, IREG_16(dest_reg), IREG_temp0_W);
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
                } else {
                    uop_SUB_IMM(ir, IREG_flags_res_W, IREG_16(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                break;

            default:
                return 0;
        }
    } else {
        x86seg  *target_seg;
        uint16_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        imm = fastreadw(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp2_W, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_ADD_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_OR_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_ADD_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_ADD(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp3_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_SUB_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_SUB(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp3_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_AND_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x28: /*SUB*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_SUB_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp2_W);
                else
                    uop_XOR_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_MOVZX(ir, IREG_flags_op2, IREG_temp2_W);
                    uop_SUB(ir, IREG_flags_res_W, IREG_temp0_W, IREG_temp2_W);
                } else {
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                    uop_SUB_IMM(ir, IREG_flags_res_W, IREG_temp0_W, imm);
                }
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    if (!skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, 2);
    return op_pc + 3;
}
uint32_t
rop81_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int skip_immediate = 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int      dest_reg = fetchdat & 7;
        uint32_t imm      = fastreadl(cs + op_pc + 1);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_ADD_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                else
                    uop_OR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_ADD_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_SUB_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                else
                    uop_AND_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_SUB_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp0);
                else
                    uop_XOR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_SUB(ir, IREG_flags_res, IREG_32(dest_reg), IREG_temp0);
                    uop_MOV(ir, IREG_flags_op2, IREG_temp0);
                } else {
                    uop_SUB_IMM(ir, IREG_flags_res, IREG_32(dest_reg), imm);
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                break;

            default:
                return 0;
        }
    } else {
        x86seg  *target_seg;
        uint32_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        imm = fastreadl(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);

        if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
            skip_immediate = 1;
            LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp2, cs + op_pc + 1);
        }

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x08: /*OR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                else
                    uop_OR_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_ADD(ir, IREG_temp1, IREG_temp1, IREG_temp3);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp3);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_SUB_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_SUB(ir, IREG_temp1, IREG_temp1, IREG_temp3);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x20: /*AND*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_AND(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                else
                    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x28: /*SUB*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_SUB(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                else
                    uop_SUB_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                else
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x30: /*XOR*/
                if (block->flags & CODEBLOCK_NO_IMMEDIATES)
                    uop_XOR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                else
                    uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x38: /*CMP*/
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                    uop_SUB(ir, IREG_flags_res, IREG_temp0, IREG_temp2);
                } else {
                    uop_MOV_IMM(ir, IREG_flags_op2, imm);
                    uop_SUB_IMM(ir, IREG_flags_res, IREG_temp0, imm);
                }
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    if (!skip_immediate)
        codegen_mark_code_present(block, cs + op_pc + 1, 4);
    return op_pc + 5;
}

uint32_t
rop83_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int      dest_reg = fetchdat & 7;
        uint16_t imm      = (int16_t) (int8_t) fastreadb(cs + op_pc + 1);

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_ADD_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x08: /*OR*/
                uop_OR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_ADD_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_ADD(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SUB_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_SUB(ir, IREG_16(dest_reg), IREG_16(dest_reg), IREG_temp1_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x20: /*AND*/
                uop_AND_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SUB_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x30: /*XOR*/
                uop_XOR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SUB_IMM(ir, IREG_flags_res_W, IREG_16(dest_reg), imm);
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                break;

            default:
                return 0;
        }
    } else {
        x86seg  *target_seg;
        uint16_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        imm = (int16_t) (int8_t) fastreadb(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_ADD_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x08: /*OR*/
                uop_OR_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp2);
                uop_ADD_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_ADD(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp2_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp2);
                uop_SUB_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_SUB(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp2_W);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x20: /*AND*/
                uop_AND_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x28: /*SUB*/
                uop_SUB_IMM(ir, IREG_temp1_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x30: /*XOR*/
                uop_XOR_IMM(ir, IREG_temp0_W, IREG_temp0_W, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN16);
                break;

            case 0x38: /*CMP*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_SUB_IMM(ir, IREG_flags_res_W, IREG_temp0_W, imm);
                uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
rop83_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int      dest_reg = fetchdat & 7;
        uint32_t imm      = (int32_t) (int8_t) fastreadb(cs + op_pc + 1);

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_ADD_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x08: /*OR*/
                uop_OR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_ADD_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_ADD(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SUB_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_SUB(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp1);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x20: /*AND*/
                uop_AND_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x28: /*SUB*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SUB_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x30: /*XOR*/
                uop_XOR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x38: /*CMP*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SUB_IMM(ir, IREG_flags_res, IREG_32(dest_reg), imm);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                break;

            default:
                return 0;
        }
    } else {
        x86seg  *target_seg;
        uint32_t imm;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if ((fetchdat & 0x38) == 0x38) /*CMP*/
            codegen_check_seg_read(block, ir, target_seg);
        else
            codegen_check_seg_write(block, ir, target_seg);
        imm = (int32_t) (int8_t) fastreadb(cs + op_pc + 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);

        switch (fetchdat & 0x38) {
            case 0x00: /*ADD*/
                uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADD32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x08: /*OR*/
                uop_OR_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x10: /*ADC*/
                get_cf(ir, IREG_temp2);
                uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_ADD(ir, IREG_temp1, IREG_temp1, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ADC32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x18: /*SBB*/
                get_cf(ir, IREG_temp2);
                uop_SUB_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_SUB(ir, IREG_temp1, IREG_temp1, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SBC32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x20: /*AND*/
                uop_AND_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x28: /*SUB*/
                uop_SUB_IMM(ir, IREG_temp1, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x30: /*XOR*/
                uop_XOR_IMM(ir, IREG_temp0, IREG_temp0, imm);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ZN32);
                break;

            case 0x38: /*CMP*/
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, imm);
                uop_SUB_IMM(ir, IREG_flags_res, IREG_temp0, imm);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

static void
rebuild_c(ir_data_t *ir)
{
    int needs_rebuild = 1;

    if (codegen_flags_changed) {
        switch (cpu_state.flags_op) {
            case FLAGS_INC8:
            case FLAGS_INC16:
            case FLAGS_INC32:
            case FLAGS_DEC8:
            case FLAGS_DEC16:
            case FLAGS_DEC32:
                needs_rebuild = 0;
                break;

            default:
                break;
        }
    }

    if (needs_rebuild) {
#if defined __aarch64__ || defined _M_ARM64
        /*flags_rebuild_c() writes only flags, so the cached registers are
          written back but left loaded, and flags is reloaded after it.*/
        uop_CALL_FUNC_ORDERED(ir, jit_flags_rebuild_c);
        uop_MOVZX_REG_PTR_16(ir, IREG_flags, &cpu_state.flags);
#else
        uop_CALL_FUNC(ir, jit_flags_rebuild_c);
#endif
    }
}

uint32_t
ropINC_r16(UNUSED(UNUSED(codeblock_t *block)), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOVZX(ir, IREG_flags_op1, IREG_16(opcode & 7));
    uop_ADD_IMM(ir, IREG_16(opcode & 7), IREG_16(opcode & 7), 1);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC16);
    codegen_flags_changed = 1;

    return op_pc;
}
uint32_t
ropINC_r32(UNUSED(UNUSED(codeblock_t *block)), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOV(ir, IREG_flags_op1, IREG_32(opcode & 7));
    uop_ADD_IMM(ir, IREG_32(opcode & 7), IREG_32(opcode & 7), 1);
    uop_MOV(ir, IREG_flags_res, IREG_32(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC32);
    codegen_flags_changed = 1;

    return op_pc;
}

uint32_t
ropDEC_r16(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOVZX(ir, IREG_flags_op1, IREG_16(opcode & 7));
    uop_SUB_IMM(ir, IREG_16(opcode & 7), IREG_16(opcode & 7), 1);
    uop_MOVZX(ir, IREG_flags_res, IREG_16(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC16);
    codegen_flags_changed = 1;

    return op_pc;
}
uint32_t
ropDEC_r32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    rebuild_c(ir);

    uop_MOV(ir, IREG_flags_op1, IREG_32(opcode & 7));
    uop_SUB_IMM(ir, IREG_32(opcode & 7), IREG_32(opcode & 7), 1);
    uop_MOV(ir, IREG_flags_res, IREG_32(opcode & 7));
    uop_MOV_IMM(ir, IREG_flags_op2, 1);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC32);
    codegen_flags_changed = 1;

    return op_pc;
}

uint32_t
ropINCDEC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    codegen_mark_code_present(block, cs + op_pc, 1);
    rebuild_c(ir);

    if ((fetchdat & 0xc0) == 0xc0) {
        uop_MOVZX(ir, IREG_flags_op1, IREG_8(fetchdat & 7));
        if (fetchdat & 0x38) {
            uop_SUB_IMM(ir, IREG_8(fetchdat & 7), IREG_8(fetchdat & 7), 1);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC8);
        } else {
            uop_ADD_IMM(ir, IREG_8(fetchdat & 7), IREG_8(fetchdat & 7), 1);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC8);
        }
        uop_MOVZX(ir, IREG_flags_res, IREG_8(fetchdat & 7));
        uop_MOV_IMM(ir, IREG_flags_op2, 1);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);

        if (fetchdat & 0x38) {
            uop_SUB_IMM(ir, IREG_temp1_B, IREG_temp0_B, 1);
        } else {
            uop_ADD_IMM(ir, IREG_temp1_B, IREG_temp0_B, 1);
        }
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
        uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
        uop_MOV_IMM(ir, IREG_flags_op2, 1);
        if (fetchdat & 0x38) {
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_DEC8);
        } else {
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_INC8);
        }
    }

    return op_pc + 1;
}

/*IMUL keeps SF/ZF/AF/PF from the previous result and sets CF and OF together
  when the signed product does not fit in the destination, as the interpreter
  does. flags_rebuild() writes only flags and flags_op, so it is called with
  the cached registers written back but left loaded, and those two are
  reloaded after it. Afterwards flags_op is FLAGS_UNKNOWN, so a following
  IMUL with no flag-setting instruction in between skips the call.*/
static void
imul_rebuild_flags(ir_data_t *ir)
{
    if (!codegen_flags_changed || cpu_state.flags_op != FLAGS_UNKNOWN) {
        uop_CALL_FUNC_ORDERED(ir, jit_flags_rebuild);
        uop_MOVZX_REG_PTR_16(ir, IREG_flags, &cpu_state.flags);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_UNKNOWN);
    }
    codegen_flags_changed = 1;
}

/*The interpreter path subtracted the cycles accumulated so far before IMUL
  ran, so a fault in it, or in a later instruction of the block, was taken
  with them charged. The compiled forms flush at the same point.*/
static int
imul_supported(void)
{
#if defined __aarch64__ || defined _M_ARM64
    return 1;
#else
    return 0;
#endif
}

static int
imul_load_rm(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t *op_pc, int is_32)
{
    x86seg *target_seg;

    if ((fetchdat & 0xc0) == 0xc0)
        return is_32 ? IREG_32(fetchdat & 7) : IREG_16(fetchdat & 7);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_REG(ir, is_32 ? IREG_temp0 : IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    return is_32 ? IREG_temp0 : IREG_temp0_W;
}

static void
imul_generate(ir_data_t *ir, int dest_reg, int src_reg_a, int src_reg_b)
{
    uop_IMUL_OVERFLOW(ir, IREG_temp2, src_reg_a, src_reg_b);
    uop_IMUL(ir, dest_reg, src_reg_a, src_reg_b);
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp2, 11);
    uop_OR(ir, IREG_temp3, IREG_temp3, IREG_temp2);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~(C_FLAG | V_FLAG));
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp3_W);
}

uint32_t
ropIMUL_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src_reg;

    if (!imul_supported())
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_accumulate_flush(ir);
    imul_rebuild_flags(ir);
    src_reg = imul_load_rm(block, ir, fetchdat, op_32, &op_pc, 0);
    imul_generate(ir, IREG_16(dest_reg), IREG_16(dest_reg), src_reg);

    return op_pc + 1;
}
uint32_t
ropIMUL_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src_reg;

    if (!imul_supported())
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_accumulate_flush(ir);
    imul_rebuild_flags(ir);
    src_reg = imul_load_rm(block, ir, fetchdat, op_32, &op_pc, 1);
    imul_generate(ir, IREG_32(dest_reg), IREG_32(dest_reg), src_reg);

    return op_pc + 1;
}

uint32_t
ropIMUL_w_rm_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src_reg;

    if (!imul_supported())
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_accumulate_flush(ir);
    imul_rebuild_flags(ir);
    src_reg = imul_load_rm(block, ir, fetchdat, op_32, &op_pc, 0);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp1_W, cs + op_pc + 1);
    else {
        uop_MOV_IMM(ir, IREG_temp1_W, fastreadw(cs + op_pc + 1));
        codegen_mark_code_present(block, cs + op_pc + 1, 2);
    }
    imul_generate(ir, IREG_16(dest_reg), src_reg, IREG_temp1_W);

    return op_pc + 3;
}
uint32_t
ropIMUL_l_rm_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src_reg;

    if (!imul_supported())
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_accumulate_flush(ir);
    imul_rebuild_flags(ir);
    src_reg = imul_load_rm(block, ir, fetchdat, op_32, &op_pc, 1);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp1, cs + op_pc + 1);
    else {
        uop_MOV_IMM(ir, IREG_temp1, fastreadl(cs + op_pc + 1));
        codegen_mark_code_present(block, cs + op_pc + 1, 4);
    }
    imul_generate(ir, IREG_32(dest_reg), src_reg, IREG_temp1);

    return op_pc + 5;
}

uint32_t
ropIMUL_w_rm_imm8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src_reg;

    if (!imul_supported())
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_accumulate_flush(ir);
    imul_rebuild_flags(ir);
    src_reg = imul_load_rm(block, ir, fetchdat, op_32, &op_pc, 0);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc + 1);
        uop_MOVSX(ir, IREG_temp1_W, IREG_temp1_B);
    } else {
        uop_MOV_IMM(ir, IREG_temp1_W, (uint16_t) (int16_t) (int8_t) fastreadb(cs + op_pc + 1));
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    }
    imul_generate(ir, IREG_16(dest_reg), src_reg, IREG_temp1_W);

    return op_pc + 2;
}
uint32_t
ropIMUL_l_rm_imm8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;
    int src_reg;

    if (!imul_supported())
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    codegen_accumulate_flush(ir);
    imul_rebuild_flags(ir);
    src_reg = imul_load_rm(block, ir, fetchdat, op_32, &op_pc, 1);
    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp1_B, cs + op_pc + 1);
        uop_MOVSX(ir, IREG_temp1, IREG_temp1_B);
    } else {
        uop_MOV_IMM(ir, IREG_temp1, (uint32_t) (int32_t) (int8_t) fastreadb(cs + op_pc + 1));
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    }
    imul_generate(ir, IREG_32(dest_reg), src_reg, IREG_temp1);

    return op_pc + 2;
}
