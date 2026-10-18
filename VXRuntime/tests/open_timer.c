#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef HANDLE (WINAPI *open_timer_a)(DWORD, BOOL, LPCSTR);
_Static_assert(sizeof(HANDLE) == 8, "PE64 HANDLE");

int main(void) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    open_timer_a open_timer = (open_timer_a)GetProcAddress(k32, "OpenWaitableTimerA");
    assert(open_timer);
    char timer_name[80], event_name[80], missing[80];
    WCHAR timer_w[80];
    snprintf(timer_name, sizeof(timer_name), "vxrt-open-timer-%lu", GetCurrentProcessId());
    snprintf(event_name, sizeof(event_name), "vxrt-open-timer-event-%lu", GetCurrentProcessId());
    snprintf(missing, sizeof(missing), "vxrt-open-timer-missing-%lu", GetCurrentProcessId());
    for (unsigned i = 0; i <= strlen(timer_name); ++i) timer_w[i] = (WCHAR)timer_name[i];
    HANDLE ta = CreateWaitableTimerA(NULL, TRUE, timer_name);
    HANDLE tw = CreateWaitableTimerW(NULL, TRUE, timer_w);
    HANDLE event = CreateEventA(NULL, TRUE, FALSE, event_name);
    assert(ta && tw && event);
    const char *names[] = {NULL, "", missing, timer_name, event_name};
    DWORD rights[] = {0, SYNCHRONIZE, TIMER_ALL_ACCESS, 0xffffffffu};
    unsigned count = 0;
    // Until named timers exist, none of these calls may claim a lookup or return an event handle.
    for (unsigned dynamic = 0; dynamic < 2; ++dynamic)
        for (unsigned n = 0; n < 5; ++n)
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned inherit = 0; inherit < 2; ++inherit) {
                    char copy[80];
                    if (names[n]) strcpy(copy, names[n]);
                    SetLastError(0x12345678);
                    HANDLE h = dynamic ? open_timer(rights[r], inherit, names[n])
                        : OpenWaitableTimerA(rights[r], inherit, names[n]);
                    DWORD error = GetLastError();
                    assert(h == NULL && error == ERROR_NOT_SUPPORTED);
                    if (names[n]) assert(!strcmp(copy, names[n]));
                    ++count;
                }
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    assert(SetEvent(event));
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
    assert(CloseHandle(event));
    assert(CloseHandle(ta));
    assert(CloseHandle(tw));
    SetLastError(0x87654321);
    assert(OpenWaitableTimerA(TIMER_ALL_ACCESS, FALSE, timer_name) == NULL);
    assert(GetLastError() == ERROR_NOT_SUPPORTED);
    printf("open_timer: %u direct/dynamic unsupported calls plus post-close passed\n", count);
    return 0;
}
