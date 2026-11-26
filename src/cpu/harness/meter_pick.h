/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness meter: the table a model's opcode hook picks
 *          for an instruction, as a block of statements the including
 *          function runs. It defines T(suffix) to name the tables (a
 *          model's, or opcode_deps for meter.c) and declares last_prefix,
 *          opcode, fetchdat, mod3, timings and name; with METER_WITH_DEPS
 *          also deps and dname, the common opcode_deps table the model's
 *          hook picks beside its own.
 *          The groupings follow each model's own switch: METER_GROUPS_P6
 *          (80/82 apart from 81/83, byte shifts apart), METER_SHIFT_686
 *          (shift by imm, by 1 and by CL apart), METER_SHIFT_PENTIUM (one
 *          shift table, the CL shifts' own deps), and the default (80/82/83
 *          together, 81 apart, every shift together).
 */
#define METER_STR_(x) #x
#define METER_STR(x)  METER_STR_(x)
#ifdef METER_WITH_DEPS
#    define METER_DEPS_SET(d3, dm)                                                         \
        do {                                                                               \
            deps  = mod3 ? opcode_deps##d3 : opcode_deps##dm;                              \
            dname = mod3 ? METER_STR(opcode_deps##d3) : METER_STR(opcode_deps##dm);        \
        } while (0)
#else
#    define METER_DEPS_SET(d3, dm) (void) 0
#endif
#define METER_PICK(t3, tm, d3, dm)          \
    do {                                    \
        if (mod3) {                         \
            timings = T(t3);                \
            name    = METER_STR(T(t3));     \
        } else {                            \
            timings = T(tm);                \
            name    = METER_STR(T(tm));     \
        }                                   \
        METER_DEPS_SET(d3, dm);             \
    } while (0)

switch (last_prefix) {
    case 0x0f:
#ifdef METER_HAS_0F0F
        if (opcode == 0x0f) {
            METER_PICK(_0f0f_mod3, _0f0f, _0f0f_mod3, _0f0f);
            opcode = METER_0F0F_SUFFIX;
            break;
        }
#endif
        METER_PICK(_0f_mod3, _0f, _0f_mod3, _0f);
        break;
    case 0xd8:
        METER_PICK(_d8_mod3, _d8, _d8_mod3, _d8);
        opcode = (opcode >> 3) & 7;
        break;
    case 0xd9:
        METER_PICK(_d9_mod3, _d9, _d9_mod3, _d9);
        opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
        break;
    case 0xda:
        METER_PICK(_da_mod3, _da, _da_mod3, _da);
        opcode = (opcode >> 3) & 7;
        break;
    case 0xdb:
        METER_PICK(_db_mod3, _db, _db_mod3, _db);
        opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
        break;
    case 0xdc:
        METER_PICK(_dc_mod3, _dc, _dc_mod3, _dc);
        opcode = (opcode >> 3) & 7;
        break;
    case 0xdd:
        METER_PICK(_dd_mod3, _dd, _dd_mod3, _dd);
        opcode = (opcode >> 3) & 7;
        break;
    case 0xde:
        METER_PICK(_de_mod3, _de, _de_mod3, _de);
        opcode = (opcode >> 3) & 7;
        break;
    case 0xdf:
        METER_PICK(_df_mod3, _df, _df_mod3, _df);
        opcode = (opcode >> 3) & 7;
        break;
    default:
        switch (opcode) {
#ifdef METER_GROUPS_P6
            case 0x80:
            case 0x82:
                METER_PICK(_80_mod3, _80, _8x_mod3, _8x);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0x81:
            case 0x83:
                METER_PICK(_8x_mod3, _8x, _8x_mod3, _8x);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xc0:
            case 0xd0:
            case 0xd2:
                METER_PICK(_shift_b_mod3, _shift_b, _shift_mod3, _shift);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xc1:
            case 0xd1:
            case 0xd3:
                METER_PICK(_shift_mod3, _shift, _shift_mod3, _shift);
                opcode = (fetchdat >> 3) & 7;
                break;
#else
            case 0x80:
            case 0x82:
            case 0x83:
                METER_PICK(_8x_mod3, _8x, _8x_mod3, _8x);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0x81:
                METER_PICK(_81_mod3, _81, _81_mod3, _81);
                opcode = (fetchdat >> 3) & 7;
                break;
#    ifdef METER_SHIFT_686
            case 0xc0:
            case 0xc1:
                METER_PICK(_shift_imm_mod3, _shift_imm, _shift_mod3, _shift);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xd0:
            case 0xd1:
                METER_PICK(_shift_mod3, _shift, _shift_mod3, _shift);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xd2:
            case 0xd3:
                METER_PICK(_shift_cl_mod3, _shift_cl, _shift_cl_mod3, _shift_cl);
                opcode = (fetchdat >> 3) & 7;
                break;
#    elif defined(METER_SHIFT_PENTIUM)
            case 0xc0:
            case 0xc1:
            case 0xd0:
            case 0xd1:
                METER_PICK(_shift_mod3, _shift, _shift_mod3, _shift);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xd2:
            case 0xd3:
                METER_PICK(_shift_mod3, _shift, _shift_cl_mod3, _shift_cl);
                opcode = (fetchdat >> 3) & 7;
                break;
#    else
            case 0xc0:
            case 0xc1:
            case 0xd0:
            case 0xd1:
            case 0xd2:
            case 0xd3:
                METER_PICK(_shift_mod3, _shift, _shift_mod3, _shift);
                opcode = (fetchdat >> 3) & 7;
                break;
#    endif
#endif
            case 0xf6:
                METER_PICK(_f6_mod3, _f6, _f6_mod3, _f6);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xf7:
                METER_PICK(_f7_mod3, _f7, _f7_mod3, _f7);
                opcode = (fetchdat >> 3) & 7;
                break;
            case 0xff:
                METER_PICK(_ff_mod3, _ff, _ff_mod3, _ff);
                opcode = (fetchdat >> 3) & 7;
                break;
            default:
                METER_PICK(_mod3, , _mod3, );
                break;
        }
}
