/* perf_counters.h — read CPU cycles + retired instructions via the Linux perf
 * subsystem, avoiding the rdcycle/rdinstret CSR reads that trap in user mode on
 * this board (mcounteren/scounteren don't delegate CY/IR to U-mode).
 *
 * Requires: a kernel with the RISC-V SBI PMU driver. Run as root (you are) so
 * perf_event_paranoid doesn't restrict you. This core lacks Sscofpmf, so the
 * counters run unfiltered (all privilege modes) — see the note in
 * perf__open_one below.
 *
 * Compile with -D_GNU_SOURCE. Best built with the riscv64-unknown-linux-gnu
 * toolchain (it ships <linux/perf_event.h> and produces proper Linux ELFs). If
 * you must stay on the newlib (riscv64-unknown-elf) toolchain, you'll need to
 * supply the perf_event_attr struct + SYS_perf_event_open number yourself, since
 * the bare-metal toolchain has no Linux UAPI headers.
 *
 * Usage (mirrors your existing code, same output labels so the capture harness
 * needs no change):
 *
 *     struct perf_counters pc;
 *     if (perf_counters_open(&pc) != 0) {  // fall back to clock()/etc. }
 *     perf_counters_start(&pc);
 *     enum sat_result r = solve(&f, &a);
 *     perf_counters_stop(&pc);
 *     uint64_t cycles = 0, instret = 0;
 *     perf_counters_read(&pc, &cycles, &instret);
 *     fprintf(stderr, "c cycle count: %lu cycles\n", (unsigned long)cycles);
 *     fprintf(stderr, "c solve instret: %lu\n",       (unsigned long)instret);
 *     perf_counters_close(&pc);
 *
 * For your per-phase instret breakdown: you can read() the instret fd at each
 * phase boundary while the counter stays enabled and take deltas, instead of
 * one read at the end.
 */
#ifndef PERF_COUNTERS_H
#define PERF_COUNTERS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

struct perf_counters { int cyc_fd; int ins_fd; };

static int perf__open_one(uint32_t type, uint64_t config, int group_fd) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type    = type;
    pe.size    = sizeof(pe);
    pe.config  = config;
    pe.disabled = (group_fd == -1);
    /* This core has no Sscofpmf extension, so the PMU can't filter by privilege
     * mode — requesting exclude_kernel/exclude_hv would make perf_event_open
     * fail. So we count ALL modes. On a quiet barebones system a compute-bound
     * solve() is almost entirely user cycles; the small kernel/IRQ overhead in
     * the measured window is consistent across runs, so scalar-vs-vector
     * comparisons stay valid (absolute counts run a hair above a pure-user
     * spike measurement). Run as root (you are) so paranoid=2 doesn't apply. */
    pe.exclude_kernel = 0;
    pe.exclude_hv     = 0;
    return (int)syscall(SYS_perf_event_open, &pe, 0, -1, group_fd, 0);
}

/* Open cycles + instructions as a group so they start/stop together. Returns 0
 * on success, -1 if the kernel has no usable PMU (then fall back). */
static int perf_counters_open(struct perf_counters *pc) {
    pc->cyc_fd = perf__open_one(PERF_TYPE_HARDWARE,
                                PERF_COUNT_HW_CPU_CYCLES, -1);
    if (pc->cyc_fd < 0) return -1;
    pc->ins_fd = perf__open_one(PERF_TYPE_HARDWARE,
                                PERF_COUNT_HW_INSTRUCTIONS, pc->cyc_fd);
    if (pc->ins_fd < 0) { close(pc->cyc_fd); pc->cyc_fd = -1; return -1; }
    return 0;
}

static void perf_counters_start(struct perf_counters *pc) {
    ioctl(pc->cyc_fd, PERF_EVENT_IOC_RESET,  PERF_IOC_FLAG_GROUP);
    ioctl(pc->cyc_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
}

static void perf_counters_stop(struct perf_counters *pc) {
    ioctl(pc->cyc_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
}

static int perf_counters_read(struct perf_counters *pc,
                              uint64_t *cycles, uint64_t *instret) {
    if (read(pc->cyc_fd, cycles,  sizeof(*cycles))  != (ssize_t)sizeof(*cycles))
        return -1;
    if (read(pc->ins_fd, instret, sizeof(*instret)) != (ssize_t)sizeof(*instret))
        return -1;
    return 0;
}

static void perf_counters_close(struct perf_counters *pc) {
    if (pc->ins_fd >= 0) close(pc->ins_fd);
    if (pc->cyc_fd >= 0) close(pc->cyc_fd);
}

/* ----------------------------------------------------------------------------
 * Simple global interface: drop-in replacements for `rdcycle` and your existing
 * read_instret(), so main AND the per-phase code work with minimal edits.
 *
 *   1. In exactly ONE .c file, #define PERF_IMPLEMENT_GLOBALS before including
 *      this header — that file emits the definitions of g_pc and the helpers.
 *      All other .c files get extern declarations only.
 *   2. Call counters_init() once, early in main (before the first read).
 *   3. Replace  asm volatile("rdcycle %0":"=r"(x));  with  x = read_cycle();
 *
 * read_cycle()/read_instret() return running totals since init, so the
 * existing delta logic (cycle1 - cycle0, per-phase diffs) is unchanged.
 *
 * Caveats:
 *  - Each read_*() is a syscall, not a single CSR read. Call them at phase
 *    boundaries, NOT inside hot loops.
 * ------------------------------------------------------------------------- */
extern struct perf_counters g_pc;
int      counters_init(void);
uint64_t read_cycle(void);
uint64_t read_instret(void);

#ifdef PERF_IMPLEMENT_GLOBALS
struct perf_counters g_pc = { -1, -1 };

int counters_init(void) {
    if (perf_counters_open(&g_pc) != 0) return -1;
    perf_counters_start(&g_pc);
    return 0;
}

uint64_t read_cycle(void) {
    uint64_t v = 0;
    if (g_pc.cyc_fd >= 0) (void)!read(g_pc.cyc_fd, &v, sizeof(v));
    return v;
}

uint64_t read_instret(void) {
    uint64_t v = 0;
    if (g_pc.ins_fd >= 0) (void)!read(g_pc.ins_fd, &v, sizeof(v));
    return v;
}
#endif /* PERF_IMPLEMENT_GLOBALS */

#endif /* PERF_COUNTERS_H */