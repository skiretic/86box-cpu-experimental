/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU test harness: what the CPU core, the recompiler and the
 *          memory layer reach for in the rest of the emulator, as stubs.
 *          No device, no interrupt source, no I/O: every port reads as
 *          open bus and every write is dropped.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/pic.h>
#include <86box/plat.h>

#include "harness.h"

/* Globals the core reads; set by the harness before cpu_set(). */
int      cpu             = 0;
int      cpu_use_dynarec = 0;
int      fpu_type        = 0;
int      fpu_softfloat   = 0;
int      force_10ms      = 0;
uint32_t mem_size        = 0;
int      machine         = 0;

int nmi_mask           = 0;
int inboard386_present = 0;
int is_pcjr            = 0;
int dump_missing       = 0;

int pci_burst_time    = 1;
int pci_nonburst_time = 1;
int agp_burst_time    = 1;
int agp_nonburst_time = 1;

__thread int is_cpu_thread = 1;

uint8_t io_turbo_map[65536 >> 3];

pic_t pic;

struct i8018x_pcb_t *i8018x_pcb_current = NULL;

/* cpu_set() looks at the machine's CPU package and block list; a flat
   entry that blocks nothing. */
const machine_t machines[] = {
    {
        .name          = "CPU harness",
        .internal_name = "harness",
        .cpu           = { .package = 0xffffffff, .block = NULL },
    }
};

const char *
machine_get_internal_name(void)
{
    return machines[0].internal_name;
}

int
machine_at_cobalt3k_init(const machine_t *m)
{
    (void) m;
    return 0;
}

int
machine_at_nupro592_init(const machine_t *m)
{
    (void) m;
    return 0;
}

int
machine_at_tandy_vis_init(const machine_t *m)
{
    (void) m;
    return 0;
}

int
i8018x_pcb_icu_int_type(void)
{
    return 0;
}

int
i8018x_pcb_icu_pending(void)
{
    return 0;
}

void
pcjr_waitstates(void *priv)
{
    (void) priv;
}

void
automation_reset(int hard)
{
    (void) hard;
}

void
device_reset_all(uint32_t match_flags)
{
    (void) match_flags;
}

void
dma_reset(void)
{
}

void
dma_set_at(uint8_t at)
{
    (void) at;
}

void
startblit(void)
{
}

void
endblit(void)
{
}

void
pc_speed_changed(void)
{
}

void
pci_reset(void)
{
}

void
ppi_reset(void)
{
}

void
rivatimer_init(void)
{
}

void
io_handler(uint8_t set, uint16_t base, uint16_t size,
           uint8_t (*inb)(uint16_t port, void *priv),
           uint16_t (*inw)(uint16_t port, void *priv),
           uint32_t (*inl)(uint16_t port, void *priv),
           void (*outb)(uint16_t port, uint8_t val, void *priv),
           void (*outw)(uint16_t port, uint16_t val, void *priv),
           void (*outl)(uint16_t port, uint32_t val, void *priv),
           void *priv)
{
    (void) set;
    (void) base;
    (void) size;
    (void) inb;
    (void) inw;
    (void) inl;
    (void) outb;
    (void) outw;
    (void) outl;
    (void) priv;
}

uint8_t
inb(uint16_t port)
{
    (void) port;
    return 0xff;
}

uint16_t
inw(uint16_t port)
{
    (void) port;
    return 0xffff;
}

uint32_t
inl(uint16_t port)
{
    (void) port;
    return 0xffffffff;
}

void
outb(uint16_t port, uint8_t val)
{
    (void) port;
    (void) val;
}

void
outw(uint16_t port, uint16_t val)
{
    (void) port;
    (void) val;
}

void
outl(uint16_t port, uint32_t val)
{
    (void) port;
    (void) val;
}

void
picint_common(uint16_t num, int level, int set, uint8_t *irq_state)
{
    (void) num;
    (void) level;
    (void) set;
    (void) irq_state;
}

int
picinterrupt(void)
{
    return -1;
}

uint8_t
pic_irq_ack(void)
{
    return 0;
}

unsigned
video_get_wait_states(uint32_t address, int write, unsigned size, uint64_t cpu_cycle)
{
    (void) address;
    (void) write;
    (void) size;
    (void) cpu_cycle;
    return 0;
}

void *
plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
    void *ret;

    if (large)
        *large = 0;
#if defined __APPLE__ && defined MAP_JIT
    ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
               MAP_ANON | MAP_PRIVATE | (executable ? MAP_JIT : 0), -1, 0);
#else
    ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
               MAP_ANON | MAP_PRIVATE, -1, 0);
#endif
    return (ret == MAP_FAILED) ? NULL : ret;
}

void
plat_munmap(void *ptr, size_t size)
{
    munmap(ptr, size);
}

void
pclog(const char *fmt, ...)
{
    va_list ap;

    if (!harness_verbose)
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* A fatal() from the core is a verdict on the case being run, not a
   crash: the harness prints the case and exits non-zero. */
void
fatal(const char *fmt, ...)
{
    va_list ap;
    char    buf[1024];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    harness_fatal(buf);
}
