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
 *          No device and no interrupt source; the ports are io.c's, with
 *          the harness's devices on them (harness_io.c).
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
#include <86box/thread.h>
#include <86box/pci.h>
#include <86box/m_amstrad.h>
#include <86box/sio.h>

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

/* cpu_set() writes the I/O delay (pit.c's globals). */
int io_delay       = 5;
int io_delay_multi = 0;

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

/* The flat entry has no bus, so prefetch_run() keeps the ROM prefetch
   multiply of every non-MCA machine, unless --mca gives it an MCA bus. */
int harness_mca;

int
machine_has_bus(int m, uintptr_t bus_flags)
{
    return (machines[m].bus_flags | (harness_mca ? MACHINE_BUS_MCA : 0)) & bus_flags;
}

int
machine_has_flags(int m, uintptr_t flags)
{
    return !!(machines[m].flags & flags);
}

int
debugger_instruction(int partial)
{
    (void) partial;
    return 0;
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
machine_xt_ibm5550_init(const machine_t *m)
{
    (void) m;
    return 0;
}

int
machine_xt_ibm5535_init(const machine_t *m)
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

/* What io.c reads of the board: no PCI configuration window, no Amstrad
   latch, no Super I/O watchdog. */
int      pci_flags     = 0;
uint32_t pci_base      = 0;
uint32_t pci_size      = 0;
uint32_t amstrad_latch = 0;

uint8_t
pci_read(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;
    return 0xff;
}

uint16_t
pci_readw(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;
    return 0xffff;
}

uint32_t
pci_readl(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;
    return 0xffffffff;
}

void
pci_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;
}

void
pci_writew(uint16_t port, uint16_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;
}

void
pci_writel(uint16_t port, uint32_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;
}

void
fdc37mx0x_watchdog_reset_ext(int origin)
{
    (void) origin;
}

void
picint_common(uint16_t num, int level, int set, uint8_t *irq_state)
{
    (void) level;
    (void) irq_state;
    if ((num & (1 << 13)) && set)
        harness_irq13();
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

/* No AGP aperture here. */
const uint32_t *
agpgart_cpu_entry(const mem_mapping_t *map, uint32_t addr, _Atomic uint32_t **gen, uint32_t *val, uint32_t *ra)
{
    (void) map;
    (void) addr;
    (void) gen;
    (void) val;
    (void) ra;
    return NULL;
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
   linked in. The NMI line hook and the CPU clock the local APIC timer
   scales by are the emulator's globals (nmi.c, video.c). */
uint8_t ioapic_irq_path_active = 0;
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

/* The harness runs one thread: mem.c's mapping lock has nothing to exclude. */
static int harness_mutex_dummy;

mutex_t *
thread_create_mutex(void)
{
    return &harness_mutex_dummy;
}

void
thread_close_mutex(mutex_t *arg)
{
    (void) arg;
}

int
thread_wait_mutex(mutex_t *arg)
{
    (void) arg;
    return 1;
}

int
thread_release_mutex(mutex_t *mutex)
{
    (void) mutex;
    return 1;
}
