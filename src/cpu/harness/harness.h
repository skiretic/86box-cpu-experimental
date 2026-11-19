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
/* A case's own measurements (harness_tsc.c): with REC_MEAS_MAGIC at
   REC_MEAS, the four dwords after it are printed per run, apart from the
   compared state. */
#define REC_MEAS       0x40
#define REC_MEAS_MAGIC 0x4d335821 /* "!X3M" */
#define REC_MEAS_NR    4

#define HARNESS_PAGE 4096
#define HARNESS_BODY_MAX 3072

/* Pages a case loads at physical addresses of its own choice ("@<phys>"
   in its hex text, after the body; harness_tsc.c), above the harness's. */
#define HARNESS_PAGES_MAX 4
#define HARNESS_PAGES_LOW 0x9000

typedef struct harness_page_t {
    uint32_t phys;
    uint8_t  bytes[HARNESS_PAGE];
} harness_page_t;

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
    int            pages_nr;
    harness_page_t pages[HARNESS_PAGES_MAX];
} harness_case_t;

/* What the selected CPU and FPU have, as the harness and the generator use
   it: the prelude and the epilogue read the TSC with RDTSC only where the
   CPU has it, and the generator draws only what the model has. */
typedef struct harness_model_t {
    int rdtsc;   /* RDTSC */
    int bswap;   /* BSWAP */
    int cmov;    /* CMOVcc */
    int fcmov;   /* FCMOVcc and FCOMI(P)/FUCOMI(P) */
    int mmx;     /* MMX */
    int x87;     /* an FPU */
    int x87_387; /* the 387's additions: FUCOM(P)(P), FPREM1, FSIN, FCOS, FSINCOS */
} harness_model_t;

extern harness_model_t harness_model;

/* The TSC device, for a CPU without RDTSC: an OUT to the first port takes
   the start time, an OUT to the second the time since then, both as RDTSC
   would read them, into the record page. */
#define HARNESS_PORT_TSC_START 0xec
#define HARNESS_PORT_TSC_END   0xed

extern void harness_tsc_port(uint16_t port);

/* The TSC read device: an IN from this port reads the low bits of the
   time as RDTSC would, on every CPU (harness_tsc.c). */
#define HARNESS_PORT_TSC_READ 0xea

extern uint32_t harness_tsc_read(void);

extern int  harness_pages_parse(const char *s, harness_case_t *c);
extern void harness_pages_load(const harness_case_t *c, const uint8_t *ref);
extern int  harness_pages_own(uint32_t addr);
extern void harness_pages_dump(const harness_case_t *c);
extern void harness_pages_capture(uint32_t *bytes, uint32_t *first, uint64_t *hash);
extern void harness_meas_capture(uint32_t *meas);
extern void harness_meas_print(const uint32_t *meas, const char *run);
extern void harness_rom_setup(void);

/* --rom: a 64 KB ROM window at HARNESS_ROM (harness_tsc.c), on the external
   bus and with ROM wait states, read-only to the guest; a case's extra
   pages there are loaded into it. --mca: the stub machine has an MCA bus. */
#define HARNESS_ROM      0xe0000
#define HARNESS_ROM_SIZE 0x10000

extern int harness_rom;
extern int harness_mca;

/* The SMI device: an OUT to this port asserts SMI#, taken at the next
   instruction boundary the CPU loop tests it at. */
#define HARNESS_PORT_SMI 0xee

/* A device that remaps memory when this port is read, as some cards do on
   a port read (the Sigma Color 400's 2DDh): an IN from it flushes the lookup
   tables on the CPU thread and reads as open bus. */
#define HARNESS_PORT_FLUSH 0xef

/* Where SMM code starts: SMBASE's reset value plus 8000h, and the Cyrix SMM
   region harness_smi() sets up. A case's handler is code outside the code
   page, and counts in whether a run compiled anything. */
#define HARNESS_SMM_ENTRY       0x38000
#define HARNESS_SMM_ENTRY_CYRIX 0x30000

extern void harness_smi(void);
extern void harness_irq13(void);

extern int harness_verbose;
/* The RAM writer device's latched address (stubs.c); load_case() clears it
   so every comparison run starts from the same device state. */
extern uint32_t harness_ram_writer_addr;

extern void harness_fatal(const char *msg) __attribute__((noreturn));

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
