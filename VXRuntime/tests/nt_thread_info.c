// An unsupported NT setter must report failure without changing caller state.
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void) {
    HMODULE dll = LoadLibraryA("ntdll.dll");
    CHECK(dll != NULL);
    if (!dll) return 1;
    typedef NTSTATUS (NTAPI *set_info_fn)(HANDLE, THREADINFOCLASS, PVOID, ULONG);
    set_info_fn set_info = (set_info_fn)GetProcAddress(dll, "NtSetInformationThread");
    CHECK(set_info != NULL);
    if (!set_info) return 1;

    DWORD thread_id = GetCurrentThreadId();
    int priority = GetThreadPriority(GetCurrentThread());
    unsigned char buffer[16], expected[16];
    memset(buffer, 0xa5, sizeof buffer);
    memcpy(expected, buffer, sizeof buffer);
    HANDLE handles[] = { GetCurrentThread(), NULL, (HANDLE)(ULONG_PTR)0x1234 };
    THREADINFOCLASS classes[] = { (THREADINFOCLASS)2, (THREADINFOCLASS)3, (THREADINFOCLASS)0x7fffffff };
    PVOID pointers[] = { buffer, NULL, (PVOID)(ULONG_PTR)1 };
    ULONG sizes[] = { 0, sizeof buffer, 0xffffffffu };
    unsigned calls = 0;
    for (unsigned h = 0; h < sizeof handles / sizeof handles[0]; h++)
        for (unsigned c = 0; c < sizeof classes / sizeof classes[0]; c++)
            for (unsigned p = 0; p < sizeof pointers / sizeof pointers[0]; p++)
                for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
                    SetLastError(0x12345678);
                    NTSTATUS status = set_info(handles[h], classes[c], pointers[p], sizes[s]);
                    DWORD error = GetLastError();
                    CHECK((ULONG)status == 0xc0000002u);
                    CHECK(status < 0);
                    CHECK(error == 0x12345678);
                    CHECK(memcmp(buffer, expected, sizeof buffer) == 0);
                    calls++;
                }
    CHECK(calls == 81);
    CHECK(GetCurrentThreadId() == thread_id);
    CHECK(GetThreadPriority(GetCurrentThread()) == priority);
    Sleep(0);
    CHECK(GetCurrentThreadId() == thread_id);
    CHECK(FreeLibrary(dll));
    printf("nt_thread_info: %s (%u calls)\n", failures ? "FAIL" : "PASS", calls);
    return failures ? 1 : 0;
}
