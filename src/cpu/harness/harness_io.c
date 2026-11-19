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
 *            ECh-EDh  the TSC device (harness.c)
 *            EEh      the SMI device (harness.c)
 *            EFh      the remapping device: a read flushes the lookups
 *            F0h      the same flush from another thread (harness_pg.h)
 *            500h-507h the counting device (harness_dbg.h)
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

static int
is_count_port(uint16_t port)
{
    return (port >= HARNESS_PORT_COUNT) && (port < (HARNESS_PORT_COUNT + HARNESS_PORT_COUNT_SIZE));
}

static void
harness_port_read(uint16_t port)
{
    if (port == HARNESS_PORT_FLUSH) {
        harness_flush_reads++;
        flushmmucache_nopc();
    } else if (port == HARNESS_PORT_FLUSH_OTHER)
        harness_flush_other();
    else if (is_count_port(port))
        harness_port_reads++;
}

static uint8_t
harness_inb(uint16_t port, void *priv)
{
    (void) priv;
    harness_port_read(port);
    return 0xff;
}

static uint16_t
harness_inw(uint16_t port, void *priv)
{
    (void) priv;
    harness_port_read(port);
    return 0xffff;
}

static uint32_t
harness_inl(uint16_t port, void *priv)
{
    (void) priv;
    harness_port_read(port);
    return 0xffffffff;
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
