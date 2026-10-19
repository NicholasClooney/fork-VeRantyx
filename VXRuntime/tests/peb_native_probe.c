// Standalone Darwin x86-64 probe; no runtime or guest code is linked.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__) && defined(__x86_64__)
typedef struct {
    int reentrant, ok, slot_changed, peb_changed, parameters_changed;
    int returned_peb, flags_unchanged;
} result;
static uintptr_t read_slot(void) {
    uintptr_t value;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(value) :: "memory");
    return value;
}
static void write_slot(uintptr_t value) {
    __asm__ volatile("movq %0, %%gs:0x60" :: "r"(value) : "memory");
}
static void *probe(void *arg) {
    result *r = arg;
    time_t instant = 1700000000;
    // Give libc its own buffer first, so restoring the slot also restores its
    // destructor ownership. Never leave our fake PEB installed at thread exit.
    if (!localtime(&instant)) return NULL;
    uintptr_t original = read_slot();
    unsigned char *peb = calloc(1, 4096), *parameters = calloc(1, 0x440);
    if (!peb || !parameters) { free(peb); free(parameters); return NULL; }
    uintptr_t expected = (uintptr_t)parameters, observed;
    uint32_t flags = 1, after_flags;
    memcpy(peb + 0x20, &expected, sizeof expected);
    memcpy(parameters + 8, &flags, sizeof flags);
    unsigned char before[4096];
    memcpy(before, peb, sizeof before);
    struct tm output;
    write_slot((uintptr_t)peb);
    struct tm *returned = r->reentrant ? localtime_r(&instant, &output) : localtime(&instant);
    uintptr_t after_slot = read_slot();
    write_slot(original);
    // Inspect only allocations we own, never follow a potentially corrupted
    // ProcessParameters value or the observed GS pointer.
    memcpy(&observed, peb + 0x20, sizeof observed);
    memcpy(&after_flags, parameters + 8, sizeof after_flags);
    r->ok = returned != NULL && read_slot() == original;
    r->slot_changed = after_slot != (uintptr_t)peb;
    r->peb_changed = memcmp(before, peb, sizeof before) != 0;
    r->parameters_changed = observed != expected;
    r->returned_peb = returned == (struct tm *)peb;
    r->flags_unchanged = after_flags == flags;
    free(peb); free(parameters);
    return NULL;
}
int main(void) {
    for (int mode = 0; mode < 2; mode++) {
        result r = { .reentrant = mode };
        pthread_t thread;
        if (pthread_create(&thread, NULL, probe, &r) || pthread_join(thread, NULL)) return 1;
        printf("%s: restored=%d slot_changed=%d peb_changed=%d parameters_pointer_changed=%d returned_peb=%d owned_flags_unchanged=%d\n",
               mode ? "localtime_r" : "localtime", r.ok, r.slot_changed,
               r.peb_changed, r.parameters_changed, r.returned_peb, r.flags_unchanged);
        if (!r.ok || !r.flags_unchanged) return 1;
        if (mode && (r.slot_changed || r.peb_changed || r.parameters_changed)) return 1;
    }
    return 0;
}
#else
int main(void) { puts("SKIP: requires Darwin x86-64 (Rosetta is supported)"); return 77; }
#endif
