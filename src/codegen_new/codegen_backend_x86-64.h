#include "codegen_backend_x86-64_defs.h"

/* Raised from 0x4000 - too small to hold both status variants (16/32-bit,
   flat/non-flat) of hot code at once, causing recompile thrash. This is the
   max: block indices are uint16_t. */
#define BLOCK_SIZE  0x10000
#define BLOCK_MASK  0xffff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     ((l) &0x1ffff)

#define BLOCK_MAX   0x3c0

#define CODEGEN_BACKEND_HAS_MOV_IMM

/* Blocks jump straight to their successor when nothing between them needs
   the C loop (codegen_link.h). The options below add work to every loop
   iteration that the link routine does not repeat. */
#if !defined(USE_GDBSTUB) && !defined(USE_ACYCS) && !defined(USE_DEBUG_REGS_486)
#    define CODEGEN_BACKEND_HAS_BLOCK_LINK
void codegen_link_exit(codeblock_t *block);
#endif
