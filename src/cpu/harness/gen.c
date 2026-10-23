/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the case generator. A seed draws an initial
 *          state and a body of 32-bit instructions from templates, by
 *          class. Memory operands are [ebx+disp8] into the data page,
 *          string operands are re-seated into it before every string
 *          instruction, and EBX, ESP, ESI and EDI are never written by
 *          anything else, so a case stays inside its pages. Faults are
 *          allowed (a DIV by zero, an unmasked x87 exception): the
 *          handler records them and both paths must agree.
 */
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"

/* ---- PRNG ------------------------------------------------------------ */

static uint64_t rng;

static uint32_t
rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) (rng >> 11);
}

static uint32_t
rnd_below(uint32_t n)
{
    return rnd() % n;
}

static int
chance(int percent)
{
    return (int) rnd_below(100) < percent;
}

/* ---- emit ------------------------------------------------------------ */

typedef struct emit_t {
    uint8_t *p;
    int      n;
    int      max;
    int      in_loop; /* ECX is the loop counter: not a destination */
} emit_t;

static void
e8(emit_t *e, uint8_t b)
{
    if (e->n < e->max)
        e->p[e->n] = b;
    e->n++;
}

static void
e16(emit_t *e, uint16_t v)
{
    e8(e, v & 0xff);
    e8(e, v >> 8);
}

static void
e32(emit_t *e, uint32_t v)
{
    e16(e, v & 0xffff);
    e16(e, v >> 16);
}

/* Registers never written by a generated instruction: EBX (memory base),
   ESP, ESI, EDI (string pointers). */
static int
dest_reg(const emit_t *e)
{
    static const int pool[4] = { 0, 1, 2, 5 }; /* EAX ECX EDX EBP */
    int r;

    do {
        r = pool[rnd_below(4)];
    } while (e->in_loop && r == 1);
    return r;
}

static int
src_reg(void)
{
    return (int) rnd_below(8);
}

/* An 8-bit destination: AL CL DL (AH CH DH BL BH read EBX's bytes, and
   BL/BH would write it). */
static int
dest_reg8(const emit_t *e)
{
    static const int pool[6] = { 0, 1, 2, 4, 5, 6 }; /* AL CL DL AH CH DH */
    int r;

    do {
        r = pool[rnd_below(6)];
    } while (e->in_loop && (r == 1 || r == 5));
    return r;
}

/* ModRM: register form, or [ebx+disp8] with the displacement bounded so
   an operand of up to 128 bytes stays in the data page. */
static void
modrm_reg(emit_t *e, int reg, int rm)
{
    e8(e, 0xc0 | (reg << 3) | rm);
}

static void
modrm_mem(emit_t *e, int reg)
{
    e8(e, 0x40 | (reg << 3) | 3);
    e8(e, (uint8_t) (int8_t) ((int) rnd_below(0xf8) - 0x80));
}

/* reg field + r/m: a register (dest) or memory. */
static void
modrm_rm(emit_t *e, int reg, int rm_is_mem, int rm_reg)
{
    if (rm_is_mem)
        modrm_mem(e, reg);
    else
        modrm_reg(e, reg, rm_reg);
}

/* ---- templates ------------------------------------------------------- */

static void
gen_alu(emit_t *e)
{
    static const uint8_t ops[8] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38 };
    int op   = ops[rnd_below(8)];
    int size = rnd_below(3); /* 0 byte, 1 word, 2 dword */
    int form = rnd_below(4);

    if (size == 1)
        e8(e, 0x66);
    switch (form) {
        case 0: /* op r/m, r */
            e8(e, op | (size ? 1 : 0));
            if (size)
                modrm_rm(e, src_reg(), chance(50), dest_reg(e));
            else
                modrm_rm(e, src_reg(), chance(50), dest_reg8(e));
            break;
        case 1: /* op r, r/m */
            e8(e, op | 2 | (size ? 1 : 0));
            if (size)
                modrm_rm(e, dest_reg(e), chance(50), src_reg());
            else
                modrm_rm(e, dest_reg8(e), chance(50), src_reg());
            break;
        case 2: /* op r/m, imm */
            if (!size) {
                e8(e, 0x80);
                modrm_rm(e, op >> 3, chance(50), dest_reg8(e));
                e8(e, rnd());
            } else if (chance(50)) {
                e8(e, 0x83);
                modrm_rm(e, op >> 3, chance(50), dest_reg(e));
                e8(e, rnd());
            } else {
                e8(e, 0x81);
                modrm_rm(e, op >> 3, chance(50), dest_reg(e));
                if (size == 1)
                    e16(e, rnd());
                else
                    e32(e, rnd());
            }
            break;
        default: /* TEST r/m, r or TEST r/m, imm */
            if (chance(50)) {
                e8(e, size ? 0x85 : 0x84);
                modrm_rm(e, src_reg(), chance(50), src_reg());
            } else {
                e8(e, size ? 0xf7 : 0xf6);
                modrm_rm(e, 0, chance(50), src_reg());
                if (size == 0)
                    e8(e, rnd());
                else if (size == 1)
                    e16(e, rnd());
                else
                    e32(e, rnd());
            }
            break;
    }
}

static void
gen_mov(emit_t *e)
{
    int size = rnd_below(3);

    switch (rnd_below(8)) {
        case 0: /* mov r/m, r */
            if (size == 1)
                e8(e, 0x66);
            e8(e, size ? 0x89 : 0x88);
            if (size)
                modrm_rm(e, src_reg(), chance(50), dest_reg(e));
            else
                modrm_rm(e, src_reg(), chance(50), dest_reg8(e));
            break;
        case 1: /* mov r, r/m */
            if (size == 1)
                e8(e, 0x66);
            e8(e, size ? 0x8b : 0x8a);
            if (size)
                modrm_rm(e, dest_reg(e), chance(50), src_reg());
            else
                modrm_rm(e, dest_reg8(e), chance(50), src_reg());
            break;
        case 2: /* mov r/m, imm */
            if (size == 1)
                e8(e, 0x66);
            e8(e, size ? 0xc7 : 0xc6);
            if (size)
                modrm_rm(e, 0, chance(50), dest_reg(e));
            else
                modrm_rm(e, 0, chance(50), dest_reg8(e));
            if (size == 0)
                e8(e, rnd());
            else if (size == 1)
                e16(e, rnd());
            else
                e32(e, rnd());
            break;
        case 3: /* mov r32, imm32 */
            e8(e, 0xb8 + dest_reg(e));
            e32(e, rnd());
            break;
        case 4: /* movzx / movsx r32, r/m8 or r/m16 */
            e8(e, 0x0f);
            e8(e, (chance(50) ? 0xb6 : 0xbe) | (chance(50) ? 1 : 0));
            modrm_rm(e, dest_reg(e), chance(50), src_reg());
            break;
        case 5: /* xchg r, r/m */
            if (size == 1)
                e8(e, 0x66);
            e8(e, size ? 0x87 : 0x86);
            if (size)
                modrm_rm(e, dest_reg(e), chance(50), dest_reg(e));
            else
                modrm_rm(e, dest_reg8(e), chance(50), dest_reg8(e));
            break;
        case 6: /* lea r32, [ebx+disp8] */
            e8(e, 0x8d);
            modrm_mem(e, dest_reg(e));
            break;
        default: /* bswap, cdq, cwde */
            switch (rnd_below(3)) {
                case 0:
                    e8(e, 0x0f);
                    e8(e, 0xc8 + dest_reg(e));
                    break;
                case 1:
                    if (e->in_loop)
                        e8(e, 0x98); /* cwde: EAX only */
                    else
                        e8(e, 0x99); /* cdq: EDX */
                    break;
                default:
                    e8(e, 0x98);
                    break;
            }
            break;
    }
}

static void
gen_shift(emit_t *e)
{
    int size = rnd_below(3);
    int sub  = rnd_below(8); /* ROL ROR RCL RCR SHL SHR SAL SAR */

    if (chance(25)) { /* shld / shrd */
        e8(e, 0x0f);
        if (chance(50)) {
            e8(e, chance(50) ? 0xa4 : 0xac);
            modrm_rm(e, src_reg(), chance(50), dest_reg(e));
            e8(e, rnd_below(40));
        } else {
            e8(e, chance(50) ? 0xa5 : 0xad);
            modrm_rm(e, src_reg(), chance(50), dest_reg(e));
        }
        return;
    }
    if (size == 1)
        e8(e, 0x66);
    switch (rnd_below(3)) {
        case 0: /* by imm8 */
            e8(e, size ? 0xc1 : 0xc0);
            if (size)
                modrm_rm(e, sub, chance(50), dest_reg(e));
            else
                modrm_rm(e, sub, chance(50), dest_reg8(e));
            e8(e, rnd_below(40));
            break;
        case 1: /* by 1 */
            e8(e, size ? 0xd1 : 0xd0);
            if (size)
                modrm_rm(e, sub, chance(50), dest_reg(e));
            else
                modrm_rm(e, sub, chance(50), dest_reg8(e));
            break;
        default: /* by CL */
            e8(e, size ? 0xd3 : 0xd2);
            if (size)
                modrm_rm(e, sub, chance(50), dest_reg(e));
            else
                modrm_rm(e, sub, chance(50), dest_reg8(e));
            break;
    }
}

static void
gen_mul(emit_t *e)
{
    int size = rnd_below(3);

    switch (rnd_below(4)) {
        case 0: /* mul / imul / div / idiv r/m (EAX, EDX) */
            if (size == 1)
                e8(e, 0x66);
            e8(e, size ? 0xf7 : 0xf6);
            {
                int sub = chance(90) ? (4 + (int) rnd_below(2)) : (6 + (int) rnd_below(2)); /* a divide now and then: #DE is a case too */
                modrm_rm(e, sub, chance(50), src_reg());
            }
            break;
        case 1: /* imul r, r/m */
            if (size == 1)
                e8(e, 0x66);
            e8(e, 0x0f);
            e8(e, 0xaf);
            modrm_rm(e, dest_reg(e), chance(50), src_reg());
            break;
        case 2: /* imul r, r/m, imm8 */
            if (size == 1)
                e8(e, 0x66);
            e8(e, 0x6b);
            modrm_rm(e, dest_reg(e), chance(50), src_reg());
            e8(e, rnd());
            break;
        default: /* imul r, r/m, imm32 */
            if (size == 1)
                e8(e, 0x66);
            e8(e, 0x69);
            modrm_rm(e, dest_reg(e), chance(50), src_reg());
            if (size == 1)
                e16(e, rnd());
            else
                e32(e, rnd());
            break;
    }
}

static void gen_one(emit_t *e, uint32_t classes);

static void
gen_flags(emit_t *e, uint32_t classes)
{
    int cc = rnd_below(16);

    switch (rnd_below(10)) {
        case 0: /* setcc r/m8 */
            e8(e, 0x0f);
            e8(e, 0x90 + cc);
            modrm_rm(e, 0, chance(50), dest_reg8(e));
            break;
        case 1: /* cmovcc r32, r/m32 */
            e8(e, 0x0f);
            e8(e, 0x40 + cc);
            modrm_rm(e, dest_reg(e), chance(50), src_reg());
            break;
        case 2: { /* jcc short over the next instruction */
            emit_t sub = { e->p + e->n + 2, 0, e->max - e->n - 2, e->in_loop };
            if (sub.max < 16)
                break;
            gen_one(&sub, classes & ~GEN_FLAGS);
            if (sub.n > 127)
                break;
            e8(e, 0x70 + cc);
            e8(e, sub.n);
            e->n += sub.n;
            break;
        }
        case 3: /* inc / dec */
            if (chance(50))
                e8(e, (chance(50) ? 0x40 : 0x48) + dest_reg(e));
            else {
                int size = rnd_below(3);
                if (size == 1)
                    e8(e, 0x66);
                e8(e, size ? 0xff : 0xfe);
                if (size)
                    modrm_rm(e, rnd_below(2), chance(50), dest_reg(e));
                else
                    modrm_rm(e, rnd_below(2), chance(50), dest_reg8(e));
            }
            break;
        case 4: /* neg / not */
            {
                int size = rnd_below(3);
                if (size == 1)
                    e8(e, 0x66);
                e8(e, size ? 0xf7 : 0xf6);
                if (size)
                    modrm_rm(e, 2 + rnd_below(2), chance(50), dest_reg(e));
                else
                    modrm_rm(e, 2 + rnd_below(2), chance(50), dest_reg8(e));
            }
            break;
        case 5: /* bt / bts / btr / btc */
            if (chance(50)) {
                static const uint8_t ops[4] = { 0xa3, 0xab, 0xb3, 0xbb };
                int op  = ops[rnd_below(4)];
                int reg = src_reg();
                int mem = chance(50);
                int rm  = dest_reg(e);

                if (mem) {
                    /* The register's bit offset moves the operand by
                       4 * (offset >> 5) or 2 * (offset >> 4) bytes, signed:
                       -0x400..0xbff keeps it in the data page for every
                       [ebx+disp8]. The word form ignores the upper half. */
                    int      w16 = chance(33);
                    uint32_t off = rnd_below(0x1000) - 0x400;

                    reg = dest_reg(e);
                    e8(e, 0xb8 + reg); /* mov reg, imm32 */
                    e32(e, w16 ? ((rnd() << 16) | (off & 0xffff)) : off);
                    if (w16)
                        e8(e, 0x66);
                }
                e8(e, 0x0f);
                e8(e, op);
                modrm_rm(e, reg, mem, rm);
            } else {
                e8(e, 0x0f);
                e8(e, 0xba);
                modrm_rm(e, 4 + rnd_below(4), chance(50), dest_reg(e));
                e8(e, rnd());
            }
            break;
        case 6: /* bsf / bsr */
            e8(e, 0x0f);
            e8(e, chance(50) ? 0xbc : 0xbd);
            modrm_rm(e, dest_reg(e), chance(50), src_reg());
            break;
        case 7: /* lahf / sahf / cmc / clc / stc / cld / std */
            {
                static const uint8_t ops[7] = { 0x9f, 0x9e, 0xf5, 0xf8, 0xf9, 0xfc, 0xfd };
                e8(e, ops[rnd_below(7)]);
            }
            break;
        case 8: /* adc / sbb chain on the carry from the last op */
            e8(e, chance(50) ? 0x11 : 0x19);
            modrm_rm(e, src_reg(), chance(50), dest_reg(e));
            break;
        default: /* cmp then setcc into memory */
            e8(e, 0x39);
            modrm_rm(e, src_reg(), chance(50), src_reg());
            e8(e, 0x0f);
            e8(e, 0x90 + cc);
            modrm_mem(e, 0);
            break;
    }
}

static int pushes;

static void
gen_stack(emit_t *e)
{
    switch (rnd_below(4)) {
        case 0: /* push r32 */
            if (pushes < 64) {
                e8(e, 0x50 + src_reg());
                pushes++;
            }
            break;
        case 1: /* pop r32 */
            e8(e, 0x58 + dest_reg(e));
            break;
        case 2: /* push imm */
            if (pushes < 64) {
                if (chance(50)) {
                    e8(e, 0x6a);
                    e8(e, rnd());
                } else {
                    e8(e, 0x68);
                    e32(e, rnd());
                }
                pushes++;
            }
            break;
        default: /* push r16 / pop r16 */
            e8(e, 0x66);
            if (chance(50) && pushes < 64) {
                e8(e, 0x50 + src_reg());
                pushes++;
            } else
                e8(e, 0x58 + dest_reg(e));
            break;
    }
}

static void
gen_str(emit_t *e)
{
    static const uint8_t ops[10] = { 0xa4, 0xa5, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xa6, 0xa7 };
    int op = ops[rnd_below(10)];

    /* Re-seat the pointers inside the data page and pick a small count. */
    e8(e, 0xbe); /* mov esi, imm32 */
    e32(e, HARNESS_DATA + rnd_below(0x400));
    e8(e, 0xbf); /* mov edi, imm32 */
    e32(e, HARNESS_DATA + 0x800 + rnd_below(0x400));
    if (!e->in_loop) {
        e8(e, 0xb9); /* mov ecx, imm32 */
        e32(e, rnd_below(25));
    }
    if (chance(70))
        e8(e, chance(70) ? 0xf3 : 0xf2);
    if (chance(30))
        e8(e, 0x66);
    e8(e, op);
}

static void
gen_x87(emit_t *e)
{
    int i = rnd_below(8);

    switch (rnd_below(24)) {
        case 0: /* fld m32 / m64 / m80 */
            switch (rnd_below(3)) {
                case 0: e8(e, 0xd9); modrm_mem(e, 0); break;
                case 1: e8(e, 0xdd); modrm_mem(e, 0); break;
                default: e8(e, 0xdb); modrm_mem(e, 5); break;
            }
            break;
        case 1: /* fild m16 / m32 / m64 */
            switch (rnd_below(3)) {
                case 0: e8(e, 0xdf); modrm_mem(e, 0); break;
                case 1: e8(e, 0xdb); modrm_mem(e, 0); break;
                default: e8(e, 0xdf); modrm_mem(e, 5); break;
            }
            break;
        case 2: /* fst / fstp m32 / m64, fstp m80 */
            switch (rnd_below(5)) {
                case 0: e8(e, 0xd9); modrm_mem(e, 2); break;
                case 1: e8(e, 0xd9); modrm_mem(e, 3); break;
                case 2: e8(e, 0xdd); modrm_mem(e, 2); break;
                case 3: e8(e, 0xdd); modrm_mem(e, 3); break;
                default: e8(e, 0xdb); modrm_mem(e, 7); break;
            }
            break;
        case 3: /* fist / fistp m16 / m32, fistp m64 */
            switch (rnd_below(5)) {
                case 0: e8(e, 0xdf); modrm_mem(e, 2); break;
                case 1: e8(e, 0xdf); modrm_mem(e, 3); break;
                case 2: e8(e, 0xdb); modrm_mem(e, 2); break;
                case 3: e8(e, 0xdb); modrm_mem(e, 3); break;
                default: e8(e, 0xdf); modrm_mem(e, 7); break;
            }
            break;
        case 4: /* arith m32 / m64: fadd fmul fcom fcomp fsub fsubr fdiv fdivr */
            e8(e, chance(50) ? 0xd8 : 0xdc);
            modrm_mem(e, rnd_below(8));
            break;
        case 5: /* integer arith m32 / m16 */
            e8(e, chance(50) ? 0xda : 0xde);
            modrm_mem(e, rnd_below(8));
            break;
        case 6: /* arith ST, ST(i) */
            e8(e, 0xd8);
            e8(e, 0xc0 + (rnd_below(8) << 3) + i);
            break;
        case 7: /* arith ST(i), ST: /2 and /3 are reserved in this form */
            {
                static const uint8_t subs[6] = { 0, 1, 4, 5, 6, 7 };
                e8(e, 0xdc);
                e8(e, 0xc0 + (subs[rnd_below(6)] << 3) + i);
            }
            break;
        case 8: /* arith and pop: the same six */
            {
                static const uint8_t subs[6] = { 0, 1, 4, 5, 6, 7 };
                e8(e, 0xde);
                e8(e, 0xc0 + (subs[rnd_below(6)] << 3) + i);
            }
            break;
        case 9: /* fxch, fld st(i), fst/fstp st(i), ffree */
            switch (rnd_below(5)) {
                case 0: e8(e, 0xd9); e8(e, 0xc8 + i); break;
                case 1: e8(e, 0xd9); e8(e, 0xc0 + i); break;
                case 2: e8(e, 0xdd); e8(e, 0xd0 + i); break;
                case 3: e8(e, 0xdd); e8(e, 0xd8 + i); break;
                default: e8(e, 0xdd); e8(e, 0xc0 + i); break;
            }
            break;
        case 10: /* constants */
            {
                static const uint8_t ops[7] = { 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee };
                e8(e, 0xd9);
                e8(e, ops[rnd_below(7)]);
            }
            break;
        case 11: /* fchs fabs ftst fxam */
            {
                static const uint8_t ops[4] = { 0xe0, 0xe1, 0xe4, 0xe5 };
                e8(e, 0xd9);
                e8(e, ops[rnd_below(4)]);
            }
            break;
        case 12: /* fsqrt frndint fscale fprem fprem1 fxtract f2xm1 fyl2x fptan fpatan fyl2xp1 fsincos fsin fcos */
            {
                static const uint8_t ops[14] = { 0xfa, 0xfc, 0xfd, 0xf8, 0xf5, 0xf4, 0xf0, 0xf1, 0xf2, 0xf3, 0xf9, 0xfb, 0xfe, 0xff };
                e8(e, 0xd9);
                e8(e, ops[rnd_below(14)]);
            }
            break;
        case 13: /* fucom fucomp fucompp fcom fcomp fcompp */
            switch (rnd_below(6)) {
                case 0: e8(e, 0xdd); e8(e, 0xe0 + i); break;
                case 1: e8(e, 0xdd); e8(e, 0xe8 + i); break;
                case 2: e8(e, 0xda); e8(e, 0xe9); break;
                case 3: e8(e, 0xd8); e8(e, 0xd0 + i); break;
                case 4: e8(e, 0xd8); e8(e, 0xd8 + i); break;
                default: e8(e, 0xde); e8(e, 0xd9); break;
            }
            break;
        case 14: /* fcomi fcomip fucomi fucomip (P6) */
            switch (rnd_below(4)) {
                case 0: e8(e, 0xdb); e8(e, 0xf0 + i); break;
                case 1: e8(e, 0xdf); e8(e, 0xf0 + i); break;
                case 2: e8(e, 0xdb); e8(e, 0xe8 + i); break;
                default: e8(e, 0xdf); e8(e, 0xe8 + i); break;
            }
            break;
        case 15: /* fcmovcc (P6) */
            e8(e, chance(50) ? 0xda : 0xdb);
            e8(e, 0xc0 + (rnd_below(4) << 3) + i);
            break;
        case 16: /* fnstsw ax, fnstsw m16, fnstcw m16 */
            switch (rnd_below(3)) {
                case 0: e8(e, 0xdf); e8(e, 0xe0); break;
                case 1: e8(e, 0xdd); modrm_mem(e, 7); break;
                default: e8(e, 0xd9); modrm_mem(e, 7); break;
            }
            break;
        case 17: /* fldcw from a word written just before, exceptions masked 7 times in 8 */
            {
                int8_t   d  = (int8_t) ((int) rnd_below(0xf8) - 0x80);
                uint16_t cw = (chance(87) ? 0x3f : rnd_below(0x40)) | (rnd_below(4) << 8) | (rnd_below(4) << 10) | (chance(50) ? 0x1000 : 0);
                e8(e, 0x66); e8(e, 0xc7); e8(e, 0x43); e8(e, d); e16(e, cw); /* mov word [ebx+d], cw */
                e8(e, 0xd9); e8(e, 0x6b); e8(e, d);                           /* fldcw [ebx+d] */
            }
            break;
        case 18: /* fninit fnclex fwait */
            switch (rnd_below(3)) {
                case 0: e8(e, 0xdb); e8(e, 0xe3); break;
                case 1: e8(e, 0xdb); e8(e, 0xe2); break;
                default: e8(e, 0x9b); break;
            }
            break;
        case 19: /* fincstp fdecstp fnop */
            switch (rnd_below(3)) {
                case 0: e8(e, 0xd9); e8(e, 0xf7); break;
                case 1: e8(e, 0xd9); e8(e, 0xf6); break;
                default: e8(e, 0xd9); e8(e, 0xd0); break;
            }
            break;
        case 20: /* fnstenv / fldenv, fnsave / frstor */
            switch (rnd_below(4)) {
                case 0: e8(e, 0xd9); modrm_mem(e, 6); break;
                case 1: e8(e, 0xd9); modrm_mem(e, 4); break;
                case 2: e8(e, 0xdd); modrm_mem(e, 6); break;
                default: e8(e, 0xdd); modrm_mem(e, 4); break;
            }
            break;
        case 21: /* fbld / fbstp */
            e8(e, 0xdf);
            modrm_mem(e, chance(50) ? 4 : 6);
            break;
        case 22: /* fild then fist round trip under the current RC */
            e8(e, 0xdb); modrm_mem(e, 0);
            e8(e, 0xdb); modrm_mem(e, 3);
            break;
        default: /* fld m64, fmul m64, fstp m64: the common game shape */
            e8(e, 0xdd); modrm_mem(e, 0);
            e8(e, 0xdc); modrm_mem(e, 1);
            e8(e, 0xdd); modrm_mem(e, 3);
            break;
    }
}

static void
gen_mmx(emit_t *e)
{
    int mm  = rnd_below(8);
    int mm2 = rnd_below(8);

    e8(e, 0x0f);
    switch (rnd_below(12)) {
        case 0: /* movq mm, m64 / mm */
            e8(e, 0x6f);
            modrm_rm(e, mm, chance(60), mm2);
            break;
        case 1: /* movq m64, mm */
            e8(e, 0x7f);
            modrm_mem(e, mm);
            break;
        case 2: /* movd mm, r/m32 */
            e8(e, 0x6e);
            modrm_rm(e, mm, chance(50), src_reg());
            break;
        case 3: /* movd r/m32, mm */
            e8(e, 0x7e);
            modrm_rm(e, mm, chance(50), dest_reg(e));
            break;
        case 4: /* padd / psub families */
            {
                static const uint8_t ops[16] = { 0xfc, 0xfd, 0xfe, 0xec, 0xed, 0xdc, 0xdd, 0xf8, 0xf9, 0xfa, 0xe8, 0xe9, 0xd8, 0xd9, 0xd5, 0xe5 };
                e8(e, ops[rnd_below(16)]);
                modrm_rm(e, mm, chance(50), mm2);
            }
            break;
        case 5: /* pmaddwd, pand, pandn, por, pxor */
            {
                static const uint8_t ops[5] = { 0xf5, 0xdb, 0xdf, 0xeb, 0xef };
                e8(e, ops[rnd_below(5)]);
                modrm_rm(e, mm, chance(50), mm2);
            }
            break;
        case 6: /* pcmpeq / pcmpgt */
            {
                static const uint8_t ops[6] = { 0x74, 0x75, 0x76, 0x64, 0x65, 0x66 };
                e8(e, ops[rnd_below(6)]);
                modrm_rm(e, mm, chance(50), mm2);
            }
            break;
        case 7: /* punpck */
            {
                static const uint8_t ops[6] = { 0x60, 0x61, 0x62, 0x68, 0x69, 0x6a };
                e8(e, ops[rnd_below(6)]);
                modrm_rm(e, mm, chance(50), mm2);
            }
            break;
        case 8: /* pack */
            {
                static const uint8_t ops[3] = { 0x63, 0x6b, 0x67 };
                e8(e, ops[rnd_below(3)]);
                modrm_rm(e, mm, chance(50), mm2);
            }
            break;
        case 9: /* shifts by mm / m64 */
            {
                static const uint8_t ops[8] = { 0xf1, 0xf2, 0xf3, 0xd1, 0xd2, 0xd3, 0xe1, 0xe2 };
                e8(e, ops[rnd_below(8)]);
                modrm_rm(e, mm, chance(50), mm2);
            }
            break;
        case 10: /* shifts by imm8 */
            {
                static const uint8_t ops[3] = { 0x71, 0x72, 0x73 };
                static const uint8_t subs[3] = { 2, 4, 6 }; /* psrl psra psll */
                int op  = rnd_below(3);
                int sub = (op == 2) ? (chance(50) ? 2 : 6) : subs[rnd_below(3)]; /* no psraq */
                e8(e, ops[op]);
                modrm_reg(e, sub, mm);
                e8(e, rnd_below(70));
            }
            break;
        default: /* emms */
            e8(e, 0x77);
            break;
    }
}

static void
gen_one(emit_t *e, uint32_t classes)
{
    uint32_t pick[9];
    int      n = 0;

    if (classes & GEN_ALU)   pick[n++] = GEN_ALU;
    if (classes & GEN_MOV)   pick[n++] = GEN_MOV;
    if (classes & GEN_SHIFT) pick[n++] = GEN_SHIFT;
    if (classes & GEN_MUL)   pick[n++] = GEN_MUL;
    if (classes & GEN_FLAGS) pick[n++] = GEN_FLAGS;
    if (classes & GEN_STACK) pick[n++] = GEN_STACK;
    if (classes & GEN_STR)   pick[n++] = GEN_STR;
    if (classes & GEN_X87)   pick[n++] = GEN_X87;
    if (classes & GEN_MMX)   pick[n++] = GEN_MMX;
    if (!n)
        return;
    switch (pick[rnd_below(n)]) {
        case GEN_ALU:   gen_alu(e); break;
        case GEN_MOV:   gen_mov(e); break;
        case GEN_SHIFT: gen_shift(e); break;
        case GEN_MUL:   gen_mul(e); break;
        case GEN_FLAGS: gen_flags(e, classes); break;
        case GEN_STACK: gen_stack(e); break;
        case GEN_STR:   gen_str(e); break;
        case GEN_X87:   gen_x87(e); break;
        default:        gen_mmx(e); break;
    }
}

/* A counted loop of one to three instructions: what the unroller works on. */
static void
gen_loop(emit_t *e, uint32_t classes)
{
    emit_t body = { e->p + e->n + 5, 0, e->max - e->n - 5, 1 };
    int    count = 2 + rnd_below(11);
    int    n = 1 + rnd_below(3);

    if (body.max < 64)
        return;
    for (int i = 0; i < n; i++)
        gen_one(&body, classes & ~(GEN_STR | GEN_STACK));
    if (body.n > 120)
        return;
    e8(e, 0xb9); /* mov ecx, count */
    e32(e, count);
    e->n += body.n;
    if (chance(50)) {
        e8(e, 0x49);                     /* dec ecx */
        e8(e, 0x75);                     /* jnz back */
        e8(e, (uint8_t) -(body.n + 3));
    } else {
        e8(e, 0xe2);                     /* loop back */
        e8(e, (uint8_t) -(body.n + 2));
    }
}

/* ---- initial state ---------------------------------------------------- */

static double
rnd_double(void)
{
    switch (rnd_below(16)) {
        case 0: return 0.0;
        case 1: return -0.0;
        case 2: return 1.0;
        case 3: return -1.0;
        case 4: return (double) (int32_t) rnd();
        case 5: return (double) (int16_t) rnd();
        case 6: return INFINITY;
        case 7: return -INFINITY;
        case 8: return NAN;
        case 9: return 4.9406564584124654e-324; /* denormal */
        case 10: return 0.5 + rnd_below(1000) / 7.0;
        case 11: return ldexp((double) rnd(), (int) rnd_below(200) - 100);
        case 12: return 3.14159265358979323846;
        case 13: return 1e300 * (chance(50) ? 1.0 : -1.0);
        default: return (double) rnd() / (double) (rnd() | 1);
    }
}

static const struct {
    const char *name;
    uint32_t    bit;
} class_table[] = {
    { "alu", GEN_ALU }, { "mov", GEN_MOV }, { "shift", GEN_SHIFT }, { "mul", GEN_MUL },
    { "flags", GEN_FLAGS }, { "stack", GEN_STACK }, { "str", GEN_STR }, { "x87", GEN_X87 },
    { "mmx", GEN_MMX }, { "all", GEN_ALL },
};

const char *
harness_gen_class_names(void)
{
    return "alu,mov,shift,mul,flags,stack,str,x87,mmx,all";
}

int
harness_gen_classes(const char *spec, uint32_t *classes)
{
    uint32_t out = 0;

    while (*spec) {
        const char *end = strchr(spec, ',');
        size_t      len = end ? (size_t) (end - spec) : strlen(spec);
        int         found = 0;

        for (size_t i = 0; i < sizeof(class_table) / sizeof(class_table[0]); i++) {
            if (strlen(class_table[i].name) == len && !strncmp(spec, class_table[i].name, len)) {
                out |= class_table[i].bit;
                found = 1;
            }
        }
        if (!found)
            return 0;
        spec = end ? end + 1 : spec + len;
    }
    *classes = out;
    return 1;
}

void
harness_gen_case(harness_case_t *c, uint64_t seed, uint32_t classes)
{
    emit_t e;
    int    n;
    char  *l;

    memset(c, 0, sizeof(*c));
    c->generated = 1;
    rng = seed * 0x9e3779b97f4a7c15ULL + 0x2545f4914f6cdd1dULL;
    for (int i = 0; i < 8; i++)
        rnd();

    for (int i = 0; i < 8; i++)
        c->regs[i] = rnd();
    c->regs[3] = HARNESS_DATA + 0x100 + (rnd_below(0xd00) & ~7); /* EBX */
    c->regs[4] = HARNESS_ESP0;
    c->regs[6] = HARNESS_DATA + rnd_below(0x400);
    c->regs[7] = HARNESS_DATA + 0x800 + rnd_below(0x400);
    c->eflags  = (rnd() & 0x0cd5) | 2;

    c->npxc = chance(60) ? 0x037f : (0x003f | (rnd_below(4) << 8) | (rnd_below(4) << 10));
    c->top  = rnd_below(8);
    for (int i = 0; i < 8; i++) {
        if (chance(30)) {
            c->tag[i] = 0; /* empty */
            c->st[i]  = 0.0;
        } else {
            c->tag[i] = 1; /* valid */
            c->st[i]  = rnd_double();
        }
        c->mm[i] = ((uint64_t) rnd() << 32) | rnd();
    }
    for (int i = 0; i < HARNESS_PAGE; i++)
        c->data[i] = rnd();
    /* Some aligned doubles and words in the page that read as sane numbers */
    for (int i = 0; i < 64; i++) {
        double d = rnd_double();
        memcpy(c->data + 0x100 + i * 8, &d, 8);
        memcpy(c->data + 0x800 + i * 8, &d, 8);
    }

    e.p      = c->body;
    e.n      = 0;
    e.max    = HARNESS_BODY_MAX - 16;
    e.in_loop = 0;
    pushes   = 0;
    n = 6 + rnd_below(43);
    for (int i = 0; i < n && e.n < e.max - 64; i++) {
        if ((classes & (GEN_ALU | GEN_MOV | GEN_SHIFT | GEN_FLAGS | GEN_X87 | GEN_MMX)) && chance(8))
            gen_loop(&e, classes);
        else
            gen_one(&e, classes);
    }
    if (e.n > e.max)
        e.n = e.max;
    c->body_len = e.n;

    l = c->label;
    l += sprintf(l, "seed %" PRIu64 " [", seed);
    for (size_t i = 0; i + 1 < sizeof(class_table) / sizeof(class_table[0]); i++)
        if (classes & class_table[i].bit)
            l += sprintf(l, "%s%s", (l[-1] == '[') ? "" : ",", class_table[i].name);
    sprintf(l, "] %d bytes", c->body_len);
}
