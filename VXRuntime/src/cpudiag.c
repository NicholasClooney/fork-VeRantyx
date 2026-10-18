// Explicit, one-shot CPU PC sampling. No unwind, thread names, or guest payload reads.
#include "vx.h"
#include "cpudiag.h"
#include <errno.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#define CAP 128
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "signal atomics must be lock-free");
static _Atomic unsigned active, head, ready[CAP], done;
static uintptr_t pcs[CAP];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int enabled, started, joinable;
static char report[3072] = "{\"status\":\"off\"}";
static void on_sample(int sig, siginfo_t *info, void *context) {
    int saved = errno;
    if (atomic_load_explicit(&active, memory_order_relaxed)) {
        unsigned n = atomic_fetch_add_explicit(&head, 1, memory_order_relaxed);
        if (n < CAP) {
#ifdef __x86_64__
            pcs[n] = context ? ((ucontext_t *)context)->uc_mcontext->__ss.__rip : 0;
#else
            pcs[n] = 0;
#endif
            atomic_store_explicit(&ready[n], 1, memory_order_release);
        }
    }
    errno = saved;
}
static int monotonic_ns(uint64_t *out) {
    struct timespec t = {0};
#ifdef CPUDIAG_TEST_CLOCK_FAILURE
    static unsigned calls;
    if (calls++ == 1) { errno = EIO; return -1; }
#endif
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    *out = (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
    return 0;
}
static const char *module_ids[] = {"unknown", "main_image", "runtime", "vrclient_x64", "openvr_api", "msvcr100", "msvcp100", "ucrtbase", "vcruntime140", "msvcp140", "system_kernel", "system_pthread", "system_c", "system_dispatch", "system_objc"};
static unsigned classify(uintptr_t pc, uintptr_t *offset) {
    static const char *guest[] = {"vrclient_x64.dll", "openvr_api.dll", "msvcr100.dll", "msvcp100.dll", "ucrtbase.dll", "vcruntime140.dll", "msvcp140.dll"};
    static const char *host[] = {"/usr/lib/system/libsystem_kernel.dylib", "/usr/lib/system/libsystem_pthread.dylib", "/usr/lib/system/libsystem_c.dylib", "/usr/lib/system/libdispatch.dylib", "/usr/lib/libobjc.A.dylib"};
    const vx_image *m = vx_image_at(pc);
    if (m) {
        if (m->unloaded || !m->unix_path || pc < (uintptr_t)m->base || pc - (uintptr_t)m->base >= m->size) return 0;
        const char *name = strrchr(m->unix_path, '/');
        if (!name) return 0;
        for (unsigned i = 0; i < 7; i++) if (!strcmp(m->name, guest[i]) && !strcmp(name + 1, guest[i])) {
            *offset = pc - (uintptr_t)m->base; return i + 3;
        }
        return 0;
    }
    Dl_info info;
    if (!dladdr((void *)pc, &info) || !info.dli_fname || !info.dli_fbase) return 0;
    for (unsigned i = 0; i < 5; i++) if (!strcmp(info.dli_fname, host[i])) {
        for (uint32_t j = 0; j < _dyld_image_count(); j++) {
            const struct mach_header_64 *h = (const void *)_dyld_get_image_header(j);
            const char *path = _dyld_get_image_name(j);
            if ((const void *)h != info.dli_fbase || !path || strcmp(path, host[i]) || h->magic != MH_MAGIC_64) continue;
            const struct load_command *lc = (const void *)(h + 1);
            for (uint32_t k = 0; k < h->ncmds; k++) {
                if (lc->cmd == LC_SEGMENT_64) {
                    const struct segment_command_64 *seg = (const void *)lc;
                    uintptr_t lo = seg->vmaddr + _dyld_get_image_vmaddr_slide(j);
                    if ((seg->initprot & 4) && pc >= lo && pc - lo < seg->vmsize) {
                        *offset = pc - (uintptr_t)info.dli_fbase; return i + 10;
                    }
                }
                lc = (const void *)((const char *)lc + lc->cmdsize);
            }
        }
    }
    return 0;
}
#ifdef CPUDIAG_TEST
unsigned vx_cpudiag_test_classify(uintptr_t pc, uintptr_t *offset) { return classify(pc, offset); }
#endif
static int timer_busy(const struct itimerval *it) {
    return it->it_value.tv_sec || it->it_value.tv_usec || it->it_interval.tv_sec || it->it_interval.tv_usec;
}
static void status(const char *value) { snprintf(report, sizeof report, "{\"status\":\"%s\"}", value); }
static void *collect(void *unused) {
    struct sigaction old, sa = {0};
    struct itimerval previous, zero = {0}, timer = {{0, 20000}, {0, 20000}};
    const vx_image *main = vx_image_at((uintptr_t)vx_proc.image_base);
    uintptr_t base = (uintptr_t)vx_proc.image_base, size = vx_proc.image_size;
    uintptr_t rtbase = (uintptr_t)_dyld_get_image_header(0), rtlo = 0, rthi = 0;
    const struct mach_header_64 *mh = (const void *)rtbase;
    if (mh && mh->magic == MH_MAGIC_64) {
        const struct load_command *lc = (const void *)(mh + 1);
        for (uint32_t i = 0; i < mh->ncmds; i++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *s = (const void *)lc;
                if (!strcmp(s->segname, "__TEXT")) {
                    rtlo = s->vmaddr + _dyld_get_image_vmaddr_slide(0); rthi = rtlo + s->vmsize;
                }
            }
            lc = (const void *)((const char *)lc + lc->cmdsize);
        }
    }
    if (getenv("VXRT_PROFILE")) { status("conflict"); goto finished; }
#ifndef __x86_64__
    status("unsupported_arch"); goto finished;
#endif
    if (sigaction(SIGPROF, NULL, &old) || getitimer(ITIMER_PROF, &previous)) { status("system_error"); goto finished; }
    if (old.sa_handler != SIG_DFL || timer_busy(&previous)) { status("conflict"); goto finished; }
    sa.sa_sigaction = on_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    // Keep this inactive handler until exit: a pending SIGPROF must never reach SIG_DFL.
    if (sigaction(SIGPROF, &sa, &old)) { status("system_error"); goto finished; }
    if (old.sa_handler != SIG_DFL) {
        sigaction(SIGPROF, &old, NULL); status("conflict"); goto finished;
    }
    if (getitimer(ITIMER_PROF, &previous) || timer_busy(&previous)) { status("conflict"); goto finished; }
    uint64_t start, now;
    if (monotonic_ns(&start)) { status("system_error"); goto finished; }
    atomic_store_explicit(&active, 1, memory_order_relaxed);
    if (setitimer(ITIMER_PROF, &timer, NULL)) {
        atomic_store(&active, 0); status("system_error"); goto finished;
    }
    int clock_error = 0;
    for (;;) {
        if (monotonic_ns(&now)) { clock_error = 1; break; }
        if (now - start >= 2000000000ULL) break;
        struct timespec nap = {0, 10000000}; nanosleep(&nap, NULL);
    }
    atomic_store_explicit(&active, 0, memory_order_relaxed);
    int timer_error = setitimer(ITIMER_PROF, &zero, NULL);
    if (clock_error) { status("system_error"); goto finished; }
    unsigned elapsed = (unsigned)((now - start) / 1000000);
    struct sigaction current;
    int conflict = sigaction(SIGPROF, NULL, &current) || current.sa_sigaction != on_sample;
    struct { unsigned module, weight; uintptr_t offset; } bins[32] = {{0}};
    unsigned used = 0, unknown = 0, other = 0, unpublished = 0;
    unsigned count = atomic_load_explicit(&head, memory_order_relaxed);
    int mapping = main && !main->unloaded && main->base == (void *)base && main->size == size;
    for (unsigned i = 0; i < count && i < CAP; i++) {
        if (!atomic_load_explicit(&ready[i], memory_order_acquire)) { unpublished++; continue; }
        uintptr_t pc = pcs[i], offset = 0; unsigned module = 0;
        if (mapping && pc >= base && pc - base < size) { module = 1; offset = pc - base; }
        else if (rtlo && pc >= rtlo && pc < rthi) { module = 2; offset = pc - rtbase; }
        if (!module) module = classify(pc, &offset);
        if (!module) { unknown++; continue; }
        unsigned b = 0;
        for (; b < used; b++) if (bins[b].module == module && bins[b].offset == offset) break;
        if (b == used) {
            if (used == 32) { other++; continue; }
            bins[b].module = module; bins[b].offset = offset; used++;
        }
        bins[b].weight++;
    }
    const char *state = timer_error ? "timer_error" : conflict ? "conflict" : elapsed > 2250 ? "window_overrun" : "complete";
    int len = snprintf(report, sizeof report, "{\"status\":\"%s\",\"elapsed_ms\":%u,\"samples\":%u,\"overflow\":%s,\"dropped\":%u,\"unknown\":%u,\"unpublished\":%u,\"other\":%u,\"hotspots\":[", state, elapsed, count > CAP ? CAP : count, count > CAP ? "true" : "false", count > CAP ? count - CAP : 0, unknown, unpublished, other);
    // Failed windows retain counts but never expose apparent valid hotspots.
    if (!strcmp(state, "complete")) for (unsigned i = 0; i < used; i++)
        len += snprintf(report + len, sizeof report - len, "%s{\"module\":\"%s\",\"offset\":%llu,\"weight\":%u}", i ? "," : "", module_ids[bins[i].module], (unsigned long long)bins[i].offset, bins[i].weight);
    snprintf(report + len, sizeof report - len, "]}");
finished:
    atomic_store_explicit(&done, 1, memory_order_release);
    return NULL;
}
void vx_cpudiag_enable(void) {
    pthread_mutex_lock(&lock);
    if (!enabled) { enabled = 1; status("armed"); atexit(vx_cpudiag_finish); }
    pthread_mutex_unlock(&lock);
}
void vx_cpudiag_start(void) {
    pthread_mutex_lock(&lock);
    if (enabled && !started) {
        started = 1; status("running");
        if (pthread_create(&worker, NULL, collect, NULL)) { status("thread_error"); atomic_store(&done, 1); }
        else joinable = 1;
    }
    pthread_mutex_unlock(&lock);
}
char *vx_cpudiag_snapshot(void) {
    pthread_mutex_lock(&lock);
    char *out;
    if (started && !atomic_load_explicit(&done, memory_order_acquire)) out = strdup("{\"status\":\"running\"}");
    else {
        if (joinable) { pthread_join(worker, NULL); joinable = 0; }
        out = strdup(report);
    }
    pthread_mutex_unlock(&lock);
    return out;
}
void vx_cpudiag_finish(void) {
    pthread_mutex_lock(&lock);
    if (joinable) { pthread_join(worker, NULL); joinable = 0; }
    pthread_mutex_unlock(&lock);
}
