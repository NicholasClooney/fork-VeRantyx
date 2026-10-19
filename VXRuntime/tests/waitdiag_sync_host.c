// Exercise the actual sync wrappers with a recorder double; core recording has its own tests.
#include <assert.h>
#include "../src/sync.c"
static DWORD last_error;
static unsigned depth, begins, ends, results, phases, max_depth;
static const char *last_api;
static int64_t last_result;
DWORD vx_get_last_error(void) { return last_error; }
void vx_set_last_error(DWORD e) { last_error = e; }
void vx_log(const char *fmt, ...) { (void)fmt; }
const vx_image *vx_image_at(uint64_t pc) { (void)pc; return NULL; }
vx_waitdiag_scope vx_waitdiag_begin(const char *api, uintptr_t object, uint32_t timeout, uintptr_t caller) {
    (void)object; (void)timeout; (void)caller;
    last_api = api; begins++;
    if (++depth > max_depth) max_depth = depth;
    return (vx_waitdiag_scope){0, depth};
}
void vx_waitdiag_end(vx_waitdiag_scope *scope) { assert(scope->depth == depth); depth--; ends++; }
void vx_waitdiag_phase(const char *phase) { assert(depth && phase && *phase); phases++; }
void vx_waitdiag_result(vx_waitdiag_scope *scope, int64_t result) {
    assert(scope->depth == depth); last_result = result; results++;
}
void *vx_mapping_from_shm(const char *name, uint64_t size, void (**destroy)(void *)) {
    (void)name; (void)size; (void)destroy; assert(0); return NULL;
}
int main(void) {
    assert(SwitchToThread() == 1 && last_result == 1);
    assert(SleepEx(0, 0) == 0 && last_result == 0);
    assert(WaitForMultipleObjects(0, NULL, 0, 0) == WAIT_FAILED);
    assert(last_error == 87 && last_result == WAIT_FAILED);
    vx_object *event = new_object(OBJ_EVENT);
    HANDLE h = new_handle(event);
    assert(WaitForSingleObject(h, 0) == WAIT_TIMEOUT && last_result == WAIT_TIMEOUT);
    assert(SetEvent(h) && last_result == 1);
    assert(WaitForSingleObjectEx(h, 0, 0) == 0 && last_result == 0);
    assert(SetEvent(h));
    assert(ResetEvent(h) && last_result == 1);
    assert(WaitForMultipleObjectsEx(1, &h, 0, 1, 0) == WAIT_TIMEOUT);
    assert(last_result == WAIT_TIMEOUT);
    vx_object *port = new_object(OBJ_PORT);
    HANDLE ph = new_handle(port);
    DWORD bytes = 0; uint64_t key = 0; void *ov = (void *)1;
    assert(!GetQueuedCompletionStatus(ph, &bytes, &key, &ov, 0));
    assert(last_error == WAIT_TIMEOUT && last_result == 0 && !ov);
    assert(PostQueuedCompletionStatus(ph, 17, 23, NULL));
    assert(GetQueuedCompletionStatus(ph, &bytes, &key, &ov, 0));
    assert(bytes == 17 && key == 23 && last_result == 1);
    OVERLAPPED_ENTRY entry; uint32_t removed;
    assert(!GetQueuedCompletionStatusEx(ph, &entry, 1, &removed, 1, 0));
    assert(removed == 0 && last_result == 0);
    unsigned char cs[40] = {0};
    InitializeCriticalSection(cs);
    EnterCriticalSection(cs);
    assert(TryEnterCriticalSection(cs) && last_result == 1);
    LeaveCriticalSection(cs); LeaveCriticalSection(cs);
    uint64_t cv = 0;
    EnterCriticalSection(cs);
    assert(!SleepConditionVariableCS(&cv, cs, 0));
    assert(last_error == 1460 && last_result == 0);
    LeaveCriticalSection(cs); DeleteCriticalSection(cs);
    uint64_t lock = 0;
    AcquireSRWLockExclusive(&lock);
    assert(!TryAcquireSRWLockExclusive(&lock) && last_result == 0);
    ReleaseSRWLockExclusive(&lock);
    assert(TryAcquireSRWLockShared(&lock) && last_result == 1);
    assert(!SleepConditionVariableSRW(&cv, &lock, 0, 1));
    assert(last_error == 1460 && last_result == 0);
    ReleaseSRWLockShared(&lock);
    AcquireSRWLockShared(&lock); ReleaseSRWLockShared(&lock);
    WakeConditionVariable(&cv);
    assert(!strcmp(last_api, "WakeAllConditionVariable"));
    uint32_t value = 7, compare = 7;
    assert(!WaitOnAddress(&value, &compare, sizeof value, 0));
    assert(last_error == 1460 && last_result == 0);
    compare = 8;
    assert(WaitOnAddress(&value, &compare, sizeof value, 0) && last_result == 1);
    WakeByAddressSingle(&value);
    assert(!strcmp(last_api, "WakeByAddressAll"));
    assert(depth == 0 && begins == ends && max_depth >= 2 && phases && results);
    puts("waitdiag_sync_host: PASS");
    return 0;
}
