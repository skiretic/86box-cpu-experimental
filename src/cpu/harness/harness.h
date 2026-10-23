/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: a case, and the guest memory map the cases
 *          are laid out in.
 */
#ifndef CPU_HARNESS_H
#define CPU_HARNESS_H

#include <stdint.h>

/* Guest physical layout. The code and the data sit in different pages so a
   data restore between runs never dirties a code page (and never throws a
   compiled block away). */
#define HARNESS_IDT      0x1000 /* 256 gates */
#define HARNESS_GDT      0x1800
#define HARNESS_HANDLERS 0x2000 /* 256 handlers, 32 bytes each */
#define HARNESS_RECORD   0x4000 /* what the handlers and the epilogue write */
#define HARNESS_STACK    0x5000 /* ESP starts at the top of this page */
#define HARNESS_DATA     0x6000 /* the case's data page */
#define HARNESS_CODE     0x8000 /* prelude, body, epilogue */
#define HARNESS_RAM_KB   4096

#define HARNESS_ESP0 (HARNESS_STACK + 0xff0)

/* Record page fields (dwords). */
#define REC_VECTOR    0x00 /* vector of the fault taken, 0xffffffff for none */
#define REC_TOS       0x04 /* the dword on top of the stack in the handler */
#define REC_TSC_START 0x10 /* 64-bit */
#define REC_TSC_DELTA 0x18 /* 64-bit: epilogue RDTSC minus prelude RDTSC */
#define REC_SAVE_EAX  0x20
#define REC_SAVE_EDX  0x24
#define REC_SAVE_EAX2 0x28
#define REC_SAVE_EDX2 0x2c

#define HARNESS_PAGE 4096
#define HARNESS_BODY_MAX 3072

/* Selectors in the GDT. */
#define SEL_CODE32 0x08
#define SEL_DATA32 0x10
#define SEL_CODE16 0x18
#define SEL_DATA16 0x20

typedef struct harness_case_t {
    uint32_t regs[8]; /* EAX ECX EDX EBX ESP EBP ESI EDI */
    uint32_t eflags;  /* CF PF AF ZF SF DF OF are taken; the rest is forced */
    uint16_t npxc;
    int      top;
    uint8_t  tag[8];
    double   st[8];
    uint64_t mm[8];
    int      mode16; /* 16-bit code and stack segments */
    int      generated; /* from the generator: a store to the code page is its defect */
    uint8_t  data[HARNESS_PAGE];
    uint8_t  body[HARNESS_BODY_MAX];
    int      body_len;
    char     label[128]; /* what the generator drew, for the report */
} harness_case_t;

extern int harness_verbose;

extern void harness_fatal(const char *msg);

/* gen.c */
extern void harness_gen_case(harness_case_t *c, uint64_t seed, uint32_t classes);
extern int  harness_gen_classes(const char *spec, uint32_t *classes);
extern const char *harness_gen_class_names(void);

#define GEN_ALU   (1 << 0)
#define GEN_MOV   (1 << 1)
#define GEN_SHIFT (1 << 2)
#define GEN_MUL   (1 << 3)
#define GEN_FLAGS (1 << 4) /* SETcc, Jcc, CMOVcc, ADC/SBB chains */
#define GEN_STACK (1 << 5)
#define GEN_STR   (1 << 6)
#define GEN_X87   (1 << 7)
#define GEN_MMX   (1 << 8)
#define GEN_ALL   0x1ff

#endif /* CPU_HARNESS_H */
