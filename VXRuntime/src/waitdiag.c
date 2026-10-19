// Bounded call metadata; no stack walks, guest payload reads or scheduling changes.
#include "vx.h"
#include "waitdiag.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#define THREADS 64
#define DEPTH 8
#define OBJECTS 256
#define BODY_SIZE 120000
static _Atomic unsigned enabled, claimed, overflow, last_present;
static const char *status = "off";
typedef struct {
    const char *api, *phase;
    uintptr_t caller;
    uint64_t started;
    int64_t result;
    unsigned object, has_result;
} frame;
typedef struct {
    pthread_mutex_t lock;
    unsigned depth, truncated;
    uint64_t entered, completed;
    frame frames[DEPTH], last;
} slot;
static slot slots[THREADS];
static __thread unsigned thread_slot, nesting;
static pthread_mutex_t objects_lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t objects[OBJECTS];
static unsigned object_count;
static void increment(uint64_t *n) { if (*n == UINT64_MAX) atomic_store(&overflow, 1); else ++*n; }
static uint64_t now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { atomic_store(&overflow, 1); return 0; }
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}
static unsigned token(uintptr_t object) {
    if (!object) return 0;
    unsigned i;
    // Never wait for another guest thread: it may have been suspended by the caller.
    if (pthread_mutex_trylock(&objects_lock)) { atomic_store(&overflow, 1); return 0; }
    for (i = 0; i < object_count; i++) if (objects[i] == object) break;
    if (i == object_count && i < OBJECTS) objects[object_count++] = object;
    pthread_mutex_unlock(&objects_lock);
    if (i == OBJECTS) { atomic_store(&overflow, 1); return 0; }
    return i + 1;
}
void vx_waitdiag_enable(void) {
    int saved = errno;
    // This runs on the native UI thread before guest startup. Reading Windows last-error
    // would attach a TEB and overwrite Darwin's localtime TSD slot with the shared PEB.
    // Only native operations follow, so Windows last-error is already untouched.
    // Startup-only, before guest execution. Do not reset slots if called again.
    if (strcmp(status, "off")) goto finished;
#ifndef __x86_64__
    status = "unsupported_arch";
#else
    if (getenv("VXRT_TRACE_CALLS")) { status = "conflict"; goto finished; }
    for (unsigned i = 0; i < THREADS; i++) pthread_mutex_init(&slots[i].lock, NULL);
    status = "active";
    atomic_store_explicit(&enabled, 1, memory_order_release);
#endif
finished:
    errno = saved;
}
vx_waitdiag_scope vx_waitdiag_begin(const char *api, uintptr_t object, uint32_t timeout, uintptr_t caller) {
    (void)timeout; // Deliberately omit arguments from this first metadata schema.
    vx_waitdiag_scope scope = {0};
    if (!atomic_load_explicit(&enabled, memory_order_acquire)) return scope;
    int saved = errno; DWORD error = vx_get_last_error();
    if (!thread_slot) {
        unsigned n = atomic_fetch_add(&claimed, 1);
        thread_slot = n < THREADS ? n + 1 : UINT32_MAX;
    }
    if (thread_slot == UINT32_MAX) { atomic_store(&overflow, 1); goto finished; }
    scope = (vx_waitdiag_scope){thread_slot, ++nesting};
    frame f = { .api=api, .phase="entered", .caller=caller, .started=now(), .object=token(object) };
    slot *s = &slots[thread_slot - 1];
    pthread_mutex_lock(&s->lock);
    increment(&s->entered);
    s->depth = nesting;
    if (nesting <= DEPTH) s->frames[nesting-1] = f;
    else { s->truncated = 1; atomic_store(&overflow, 1); }
    pthread_mutex_unlock(&s->lock);
finished:
    vx_set_last_error(error); errno = saved;
    return scope;
}
void vx_waitdiag_end(vx_waitdiag_scope *scope) {
    if (!scope->slot) return;
    int saved = errno; DWORD error = vx_get_last_error();
    slot *s = &slots[scope->slot-1];
    pthread_mutex_lock(&s->lock);
    if (nesting != scope->depth) { s->truncated = 1; atomic_store(&overflow, 1); }
    if (scope->depth == 1) { s->last=s->frames[0]; s->last.phase="completed"; }
    increment(&s->completed);
    nesting = scope->depth - 1;
    s->depth = nesting;
    pthread_mutex_unlock(&s->lock);
    scope->slot = 0;
    vx_set_last_error(error); errno = saved;
}
void vx_waitdiag_phase(const char *phase) {
    if (!thread_slot || thread_slot > THREADS || !nesting || nesting > DEPTH) return;
    int saved = errno; DWORD error = vx_get_last_error();
    slot *s = &slots[thread_slot-1];
    pthread_mutex_lock(&s->lock); s->frames[nesting-1].phase=phase; pthread_mutex_unlock(&s->lock);
    vx_set_last_error(error); errno = saved;
}
void vx_waitdiag_result(vx_waitdiag_scope *scope, int64_t result) {
    if (!scope->slot || scope->depth > DEPTH) return;
    int saved = errno; DWORD error = vx_get_last_error();
    slot *s = &slots[scope->slot-1];
    pthread_mutex_lock(&s->lock);
    s->frames[scope->depth-1].result=result; s->frames[scope->depth-1].has_result=1;
    pthread_mutex_unlock(&s->lock);
    vx_set_last_error(error); errno = saved;
}
void vx_waitdiag_present(void) {
    if (thread_slot && thread_slot <= THREADS) atomic_store(&last_present, thread_slot);
}
static const char *classify(uintptr_t pc, uintptr_t *offset) {
    static const char *names[] = {"vrclient_x64.dll","openvr_api.dll","msvcr100.dll","msvcp100.dll","ucrtbase.dll","vcruntime140.dll","msvcp140.dll"};
    static const char *ids[] = {"vrclient_x64","openvr_api","msvcr100","msvcp100","ucrtbase","vcruntime140","msvcp140"};
    *offset=0;
    if (!pc) return "unknown";
    const vx_image *m=vx_image_at(pc);
    if (m && !m->unloaded && pc >= (uintptr_t)m->base && pc-(uintptr_t)m->base < m->size) {
        if (m->base==vx_proc.image_base && m->size==vx_proc.image_size) { *offset=pc-(uintptr_t)m->base; return "main_image"; }
        const char *base=m->unix_path ? strrchr(m->unix_path,'/') : NULL;
        for (unsigned i=0;i<7;i++) if (base && !strcmp(base+1,names[i]) && !strcmp(m->name,names[i])) {
            *offset=pc-(uintptr_t)m->base; return ids[i];
        }
        return "unknown";
    }
    Dl_info info, self;
    if (dladdr((void *)pc,&info) && dladdr((void *)vx_waitdiag_begin,&self) && info.dli_fbase==self.dli_fbase) {
        *offset=pc-(uintptr_t)info.dli_fbase; return "runtime";
    }
    return "unknown";
}
char *vx_waitdiag_snapshot(void) {
    int saved=errno; DWORD error=vx_get_last_error();
    char *body=calloc(1,BODY_SIZE), *out=NULL;
    if (!body) goto finished;
    size_t used=0; unsigned rows=0, lost=atomic_load(&overflow);
    unsigned count=atomic_load(&claimed); if(count>THREADS) count=THREADS;
#define ADD(...) do { int n=snprintf(body+used,BODY_SIZE-used,__VA_ARGS__); if(n<0 || (size_t)n>=BODY_SIZE-used) goto failed; used+=(size_t)n; } while(0)
    for(unsigned i=0;i<count;i++) {
        slot *s=&slots[i]; frame copy[DEPTH]; unsigned depth, truncated; uint64_t entered, completed;
        // Snapshot never waits for a target thread or takes any runtime object lock.
        if(pthread_mutex_trylock(&s->lock)) { lost=1; continue; }
        depth=s->depth; truncated=s->truncated; entered=s->entered; completed=s->completed;
        if(depth) memcpy(copy,s->frames,sizeof copy); else copy[0]=s->last;
        pthread_mutex_unlock(&s->lock);
        if(!entered) continue;
        // Reserve a conservative complete-thread bound; never emit partial JSON.
        if(BODY_SIZE-used<6000) { lost=1; break; }
        ADD("%s{\"thread\":%u,\"entered\":%llu,\"completed\":%llu,\"active\":%s,\"truncated\":%s,\"frames\":[",rows++?",":"",i+1,(unsigned long long)entered,(unsigned long long)completed,depth?"true":"false",truncated?"true":"false");
        unsigned n=depth?depth:1; if(n>DEPTH)n=DEPTH;
        for(unsigned j=0;j<n;j++) {
            frame *f=&copy[j]; uintptr_t offset=0; const char *module=classify(f->caller,&offset);
            char result[32]; if(f->has_result)snprintf(result,sizeof result,"%lld",(long long)f->result);else strcpy(result,"null");
            ADD("%s{\"api\":\"%s\",\"phase\":\"%s\",\"object\":%u,\"caller_module\":\"%s\",\"caller_offset\":%llu,\"started_ns\":%llu,\"result\":%s}",j?",":"",f->api,f->phase,f->object,module,(unsigned long long)offset,(unsigned long long)f->started,result);
        }
        ADD("]}");
    }
    out=malloc(used+1024);
    if(out) snprintf(out,used+1024,"{\"status\":\"%s\",\"scope\":\"selected_process_including_loaded_dlls\",\"openvr_submit\":\"not_instrumented\",\"object_identity\":\"address_or_handle_not_lifetime\",\"max_threads\":64,\"max_depth\":8,\"overflow\":%s,\"last_present_thread\":%u,\"threads\":[%s]}",status,lost?"true":"false",atomic_load(&last_present),body);
failed:
    free(body);
finished:
    vx_set_last_error(error); errno=saved; return out;
#undef ADD
}
