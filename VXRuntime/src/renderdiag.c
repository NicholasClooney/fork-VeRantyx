// Fixed counters only: no graphics arguments, resource contents or dynamic strings.
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "renderdiag.h"
#include "vx.h"
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int enabled;
static __thread unsigned depth;
static int overflow;
typedef struct { uint64_t entered, completed, success, failure; uint32_t last; } counter;
static counter counts[RD_API_COUNT];
static uint64_t stages[RD_STAGE_COUNT], activity[RD_ACTIVITY_COUNT];
static const char *const names[] = {"D3D11CreateDevice", "D3D11CreateDeviceAndSwapChain", "IDXGIFactory.CreateSwapChain",
    "IDXGIFactory2.CreateSwapChainForHwnd", "IDXGISwapChain.Present", "IDXGISwapChain1.Present1"};
static void increment(uint64_t *n) { if (*n == UINT64_MAX) overflow = 1; else ++*n; }
void vx_renderdiag_enable(void) { __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE); }
unsigned vx_renderdiag_begin(unsigned api) {
    if (!__atomic_load_n(&enabled, __ATOMIC_ACQUIRE) || api >= RD_API_COUNT) return 0;
    int saved = errno; DWORD error = vx_get_last_error();
    unsigned ticket = ++depth == 1 ? 1 : 2;
    if (ticket == 1) { pthread_mutex_lock(&lock); increment(&counts[api].entered); pthread_mutex_unlock(&lock); }
    vx_set_last_error(error); errno = saved;
    return ticket;
}
void vx_renderdiag_end(unsigned api, unsigned ticket, int32_t hr) {
    if (!ticket || api >= RD_API_COUNT) return;
    int saved = errno; DWORD error = vx_get_last_error();
    if (depth) --depth;
    if (ticket == 1) {
        pthread_mutex_lock(&lock);
        increment(&counts[api].completed);
        increment(hr < 0 ? &counts[api].failure : &counts[api].success);
        counts[api].last = (uint32_t)hr;
        pthread_mutex_unlock(&lock);
    }
    vx_set_last_error(error); errno = saved;
}
void vx_renderdiag_stage(unsigned stage) {
    if (!__atomic_load_n(&enabled, __ATOMIC_ACQUIRE) || stage >= RD_STAGE_COUNT) return;
    int saved = errno; DWORD error = vx_get_last_error();
    pthread_mutex_lock(&lock); increment(&stages[stage]); pthread_mutex_unlock(&lock);
    vx_set_last_error(error); errno = saved;
}
void vx_renderdiag_activity(unsigned item) {
    if (!__atomic_load_n(&enabled, __ATOMIC_ACQUIRE) || item >= RD_ACTIVITY_COUNT) return;
    int saved = errno; DWORD error = vx_get_last_error();
    pthread_mutex_lock(&lock); increment(&activity[item]); pthread_mutex_unlock(&lock);
    vx_set_last_error(error); errno = saved;
}
static int process_cpu(uint64_t *ns) {
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) || ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) return 0;
    if ((uint64_t)ts.tv_sec > (UINT64_MAX - (uint64_t)ts.tv_nsec) / 1000000000) return 0;
    *ns = (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
    return 1;
}
// The formatter also serves the maximum-width and unavailable-clock fixtures.
static char *format_snapshot(const counter *copy, const uint64_t *branch, const uint64_t *work,
                             int saturated, uint64_t cpu, int valid) {
    char *out = calloc(1, 3072);
    if (!out) return NULL;
    size_t n = 0;
#define APPEND(...) do { int size = snprintf(out+n, 3072-n, __VA_ARGS__); \
    if (size < 0 || (size_t)size >= 3072-n) { free(out); return NULL; } n += (size_t)size; } while (0)
    APPEND("{\"scope\":\"selected_process_including_loaded_dlls\",\"openvr_submit\":\"not_instrumented\",\"overflow\":%s,\"apis\":[", saturated ? "true" : "false");
    for (unsigned i = 0; i < RD_API_COUNT; ++i) {
        char last[32];
        if (copy[i].completed) snprintf(last, sizeof last, "%u", copy[i].last); else strcpy(last, "null");
        APPEND("%s{\"api\":\"%s\",\"entered\":%llu,\"completed\":%llu,\"success\":%llu,\"failure\":%llu,\"last_hresult\":%s}", i ? "," : "", names[i],
            (unsigned long long)copy[i].entered, (unsigned long long)copy[i].completed, (unsigned long long)copy[i].success, (unsigned long long)copy[i].failure, last);
    }
    APPEND("],\"present_test\":%llu,\"present_no_drawable\":%llu,\"present_drawable_committed\":%llu,\"d3d12_delegate\":%llu,",
        (unsigned long long)branch[0], (unsigned long long)branch[1], (unsigned long long)branch[2], (unsigned long long)branch[3]);
    char cpu_text[32];
    if (valid) snprintf(cpu_text, sizeof cpu_text, "%llu", (unsigned long long)cpu); else strcpy(cpu_text, "null");
    APPEND("\"work_commit_main\":%llu,\"work_commit_pre\":%llu,\"window_callback_entered\":%llu,\"window_callback_completed\":%llu,\"process_cpu_ns\":%s,\"process_cpu_valid\":%s}",
        (unsigned long long)work[0], (unsigned long long)work[1], (unsigned long long)work[2], (unsigned long long)work[3], cpu_text, valid ? "true" : "false");
#undef APPEND
    return out;
}
char *vx_renderdiag_snapshot(void) {
    int saved = errno; DWORD error = vx_get_last_error();
    counter copy[RD_API_COUNT]; uint64_t branch[RD_STAGE_COUNT], work[RD_ACTIVITY_COUNT], cpu = 0; int saturated;
    pthread_mutex_lock(&lock);
    memcpy(copy, counts, sizeof copy); memcpy(branch, stages, sizeof branch);
    memcpy(work, activity, sizeof work); saturated = overflow;
    pthread_mutex_unlock(&lock);
    int valid = process_cpu(&cpu);
    char *out = format_snapshot(copy, branch, work, saturated, cpu, valid);
    vx_set_last_error(error); errno = saved;
    return out;
}
