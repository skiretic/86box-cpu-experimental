/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: the port devices, on the emulator's own port
 *          layer (io.c), so a port access runs what it runs in the
 *          emulator, the I/O breakpoint check included.
 *
 *          One handler takes every port, with all six access sizes: io.c
 *          then finds a handler for every access, charges no I/O delay for
 *          an empty port, and never splits a word or dword access into
 *          byte accesses. What it does by port:
 *            E0h-E8h  the RAM writer (below)
 *            EAh      the TSC read port: a read returns the TSC (harness_tsc.c)
 *            ECh-EDh  the TSC device (harness.c)
 *            EEh      the SMI device (harness.c)
 *            EFh      the remapping device: a read flushes the lookups
 *            F0h      the same flush from another thread (harness_pg.h)
 *            500h-507h the counting device (harness_dbg.h)
 *            508h     the mask remapping device (below)
 *          and every other port reads as open bus and drops writes.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>

#include "harness.h"
#include "harness_dbg.h"
#include "harness_pg.h"

/* A device that writes RAM the way a bus master does, past the CPU's write
   path, so a case can test that such a write drops the blocks compiled from
   the bytes it changes. A dword to 0xe0 sets the physical address; a byte,
   word or dword to 0xe4 writes it there with mem_write{b,w,l}_phys(); a dword
   to 0xe8 writes it there with mem_write_phys_block(). */
uint32_t harness_ram_writer_addr;

/* A device that moves a page onto the dirty and code-present byte arrays of
   another page, as a chipset's RAM remap or alias does.
   A dword to 508h gives the page number in bits 0-15 and the target in
   bits 16-30. The target supplies the byte arrays. An out-of-range page
   or target is ignored. A case puts back each page's own arrays before
   it halts.

   With bit 31 set, page 9 also fetches instructions from the target RAM
   page. Moving it back removes the fetch overlay. */
#define HARNESS_PORT_MASK_REMAP 0x0508
#define HARNESS_REMAP_EXEC      0x80000000u
#define HARNESS_REMAP_PAGE      9

static mem_mapping_t harness_remap_exec;

static void
harness_mask_remap(uint32_t val)
{
    const uint32_t page   = val & 0xffff;
    const uint32_t target = (val & ~HARNESS_REMAP_EXEC) >> 16;

    if ((page >= pages_sz) || (((uint64_t) (target + 1) << 12) > ((uint64_t) mem_size << 10)))
        return;
    if ((val & HARNESS_REMAP_EXEC) && (page != HARNESS_REMAP_PAGE))
        return;
    mem_set_page_byte_masks(&pages[page], target);
    if (val & HARNESS_REMAP_EXEC) {
        uint8_t *exec = &ram[target << 12];

        if (target == page) {
            if (!harness_remap_exec.enable)
                return;
            mem_mapping_disable(&harness_remap_exec);
        } else if (!harness_remap_exec.size) {
            mem_mapping_add(&harness_remap_exec, page << 12, 0x1000, NULL, NULL, NULL,
                            NULL, NULL, NULL, exec, MEM_MAPPING_INTERNAL, NULL);
        } else {
            if (harness_remap_exec.enable && (harness_remap_exec.exec == exec))
                return;
            mem_mapping_set_exec(&harness_remap_exec, exec);
            if (!harness_remap_exec.enable)
                mem_mapping_enable(&harness_remap_exec);
        }
        flushmmucache_pc();
    }
}

static int
is_count_port(uint16_t port)
{
    return (port >= HARNESS_PORT_COUNT) && (port < (HARNESS_PORT_COUNT + HARNESS_PORT_COUNT_SIZE));
}

/* What a read returns, cut to the access size: open bus but for the TSC. */
static uint32_t
harness_port_read(uint16_t port)
{
    if (port == HARNESS_PORT_FLUSH) {
        harness_flush_reads++;
        flushmmucache_nopc();
    } else if (port == HARNESS_PORT_FLUSH_OTHER)
        harness_flush_other();
    else if (port == HARNESS_PORT_TSC_READ)
        return harness_tsc_read();
    else if (is_count_port(port))
        harness_port_reads++;
    return 0xffffffff;
}

static uint8_t
harness_inb(uint16_t port, void *priv)
{
    (void) priv;
    return (uint8_t) harness_port_read(port);
}

static uint16_t
harness_inw(uint16_t port, void *priv)
{
    (void) priv;
    return (uint16_t) harness_port_read(port);
}

static uint32_t
harness_inl(uint16_t port, void *priv)
{
    (void) priv;
    return (uint32_t) harness_port_read(port);
}

static void
harness_outb(uint16_t port, uint8_t val, void *priv)
{
    (void) priv;
    if (port == 0xe4)
        mem_writeb_phys(harness_ram_writer_addr, val);
    else if ((port == HARNESS_PORT_TSC_START) || (port == HARNESS_PORT_TSC_END))
        harness_tsc_port(port);
    else if (port == HARNESS_PORT_SMI)
        harness_smi();
    else if (is_count_port(port))
        harness_port_writes++;
}

static void
harness_outw(uint16_t port, uint16_t val, void *priv)
{
    (void) priv;
    if (port == 0xe4)
        mem_writew_phys(harness_ram_writer_addr, val);
    else if (is_count_port(port))
        harness_port_writes++;
}

static void
harness_outl(uint16_t port, uint32_t val, void *priv)
{
    (void) priv;
    if (port == 0xe0)
        harness_ram_writer_addr = val;
    else if (port == 0xe4)
        mem_writel_phys(harness_ram_writer_addr, val);
    else if (port == 0xe8)
        mem_write_phys_block(&val, harness_ram_writer_addr, 4);
    else if (port == HARNESS_PORT_MASK_REMAP)
        harness_mask_remap(val);
    else if (is_count_port(port))
        harness_port_writes++;
}

/* After every hard reset: cpu_set() hands io.c the CPU's own ports (the
   Cyrix configuration registers, the coprocessor ports), which the harness
   has never served; io_init() drops them, and the harness's handler takes
   every port again. */
void
harness_io_reset(void)
{
    io_init();
    io_sethandler(0x0000, 0x8000, harness_inb, harness_inw, harness_inl, harness_outb, harness_outw, harness_outl, NULL);
    io_sethandler(0x8000, 0x8000, harness_inb, harness_inw, harness_inl, harness_outb, harness_outw, harness_outl, NULL);
}
