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
 *          open bus and every write is dropped, except the RAM writer at
 *          ports 0xe0-0xeb (outb/outw/outl below).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/pic.h>
#include <86box/plat.h>
#include <86box/mem.h>

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
pic_t pic2;
int   cascade = 0;

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

/* A device that writes RAM the way a bus master does, past the CPU's write
   path, so a case can test that such a write drops the blocks compiled from
   the bytes it changes. A dword to 0xe0 sets the physical address; a byte,
   word or dword to 0xe4 writes it there with mem_write{b,w,l}_phys(); a dword
   to 0xe8 writes it there with mem_write_phys_block(). */
uint32_t harness_ram_writer_addr;

void
outb(uint16_t port, uint8_t val)
{
    if (port == 0xe4)
        mem_writeb_phys(harness_ram_writer_addr, val);
}

void
outw(uint16_t port, uint16_t val)
{
    if (port == 0xe4)
        mem_writew_phys(harness_ram_writer_addr, val);
}

void
outl(uint16_t port, uint32_t val)
{
    if (port == 0xe0)
        harness_ram_writer_addr = val;
    else if (port == 0xe4)
        mem_writel_phys(harness_ram_writer_addr, val);
    else if (port == 0xe8)
        mem_write_phys_block(&val, harness_ram_writer_addr, 4);
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

/* The 8086 cores' dual non-cascaded 8259 path (Tandy 2000), linked but not
   run by the harness */
uint8_t
pic_any_pending(void)
{
    return pic.int_pending || (!cascade && pic2.int_pending);
}

uint8_t
pic_irq_ack_instance(pic_t *dev)
{
    (void) dev;
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
#ifdef _WIN32
    ret = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                       executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
    return ret;
#elif defined __APPLE__ && defined MAP_JIT
    ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
               MAP_ANON | MAP_PRIVATE | (executable ? MAP_JIT : 0), -1, 0);
    return (ret == MAP_FAILED) ? NULL : ret;
#else
    ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
               MAP_ANON | MAP_PRIVATE, -1, 0);
    return (ret == MAP_FAILED) ? NULL : ret;
#endif
}

void
plat_munmap(void *ptr, size_t size)
{
#ifdef _WIN32
    (void) size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
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

/* The APIC (lapic.c, ioapic.c, apic_cpu_policy.c, with the chipsets) is
   not built into the harness: a board without one, the 8259 path only,
   no local APIC feature, nothing to sync or reset; lapic_policy.c, the
   pure register rules the CPU's local APIC code sanitizes with, is
   linked in. The I/O turbo throttle, the NMI line hook and the CPU clock
   the local APIC timer scales by are the emulator's globals (io.c,
   nmi.c, video.c). */
uint8_t ioapic_irq_path_active = 0;
int     io_turbo_throttle      = 0;
double  cpuclock               = 0.0;
int (*nmi_line_level)(void)    = NULL;

void
lapic_set_index_source(lapic_index_source_t source)
{
    (void) source;
}

void
lapic_index_board_reset(uint8_t index)
{
    (void) index;
}

void
lapic_lint1_redeliver_index(uint8_t index)
{
    (void) index;
}

void
lapic_id_sync_index(uint8_t lapic_index, uint8_t id)
{
    (void) lapic_index;
    (void) id;
}

#define LAPIC_SYNC_STUB(name)                       \
    void name(uint8_t lapic_index, uint32_t val)    \
    {                                               \
        (void) lapic_index;                         \
        (void) val;                                 \
    }
LAPIC_SYNC_STUB(lapic_svr_sync_index)
LAPIC_SYNC_STUB(lapic_tpr_sync_index)
LAPIC_SYNC_STUB(lapic_ldr_sync_index)
LAPIC_SYNC_STUB(lapic_version_sync_index)
LAPIC_SYNC_STUB(lapic_ppr_sync_index)
LAPIC_SYNC_STUB(lapic_esr_sync_index)
LAPIC_SYNC_STUB(lapic_icr_lo_sync_index)
LAPIC_SYNC_STUB(lapic_icr_hi_sync_index)
LAPIC_SYNC_STUB(lapic_rrr_sync_index)
LAPIC_SYNC_STUB(lapic_lvt_timer_sync_index)
LAPIC_SYNC_STUB(lapic_lvt_thermal_sync_index)
LAPIC_SYNC_STUB(lapic_lvt_perf_sync_index)
LAPIC_SYNC_STUB(lapic_lvt_lint0_sync_index)
LAPIC_SYNC_STUB(lapic_lvt_lint1_sync_index)
LAPIC_SYNC_STUB(lapic_lvt_error_sync_index)
LAPIC_SYNC_STUB(lapic_timer_initial_sync_index)
LAPIC_SYNC_STUB(lapic_timer_divide_cfg_sync_index)
#undef LAPIC_SYNC_STUB

int
apic_cpu_feature_enabled_for_model(int model_capable)
{
    (void) model_capable;
    return 0;
}

int
ioapic_external_interrupt_pending(void)
{
    return 0;
}

int
ioapic_external_interrupt_vector(void)
{
    return -1;
}

int
ioapic_pic_fallback_allowed(void)
{
    return 1;
}

int
ioapic_lint1_raise(void)
{
    return 0;
}

void
ioapic_processor_reset(void)
{
}

void
lapic_apic_base_msr_sync_index(uint8_t lapic_index, uint64_t val)
{
    (void) lapic_index;
    (void) val;
}

void
lapic_arb_id_sync_index(uint8_t lapic_index, uint32_t apr)
{
    (void) lapic_index;
    (void) apr;
}

void
lapic_dfr_sync_index(uint8_t lapic_index, uint32_t dfr)
{
    (void) lapic_index;
    (void) dfr;
}
