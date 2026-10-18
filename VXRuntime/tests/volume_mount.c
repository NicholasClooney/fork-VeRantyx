#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *mount_a)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *mount_w)(LPCWSTR, LPWSTR, DWORD);

_Static_assert(sizeof(void *) == 8, "PE64 pointers");
_Static_assert(sizeof(WCHAR) == 2, "Windows UTF-16");

int main(void) {
    HMODULE module = GetModuleHandleA("kernel32.dll");
    assert(module);
    mount_a a = (mount_a)GetProcAddress(module, "GetVolumeNameForVolumeMountPointA");
    mount_w w = (mount_w)GetProcAddress(module, "GetVolumeNameForVolumeMountPointW");
    assert(a && w);
    char path_a[] = "C:\\";
    WCHAR path_w[] = L"C:\\";
    const char *paths_a[] = {NULL, path_a, "", "invalid"};
    const WCHAR *paths_w[] = {NULL, path_w, L"", L"invalid"};
    DWORD capacities[] = {0, 1, 2, 49, 50, 260};
    unsigned calls = 0;
    for (unsigned dynamic = 0; dynamic < 2; ++dynamic)
        for (unsigned path = 0; path < 4; ++path)
            for (unsigned cap = 0; cap < 6; ++cap)
                for (unsigned null_out = 0; null_out < 2; ++null_out) {
                    struct { BYTE before[16]; char data[260]; BYTE after[16]; } ba, ba_copy;
                    struct { BYTE before[16]; WCHAR data[260]; BYTE after[16]; } bw, bw_copy;
                    memset(&ba, 0xa5, sizeof(ba));
                    memset(&bw, 0x5a, sizeof(bw));
                    memcpy(&ba_copy, &ba, sizeof(ba));
                    memcpy(&bw_copy, &bw, sizeof(bw));
                    SetLastError(0x12345678);
                    BOOL ra = dynamic ? a(paths_a[path], null_out ? NULL : ba.data, capacities[cap])
                        : GetVolumeNameForVolumeMountPointA(paths_a[path], null_out ? NULL : ba.data, capacities[cap]);
                    DWORD ea = GetLastError();
                    assert(!ra && ea == ERROR_INVALID_FUNCTION);
                    assert(!memcmp(&ba, &ba_copy, sizeof(ba)));
                    SetLastError(0x87654321);
                    BOOL rw = dynamic ? w(paths_w[path], null_out ? NULL : bw.data, capacities[cap])
                        : GetVolumeNameForVolumeMountPointW(paths_w[path], null_out ? NULL : bw.data, capacities[cap]);
                    DWORD ew = GetLastError();
                    assert(!rw && ew == ERROR_INVALID_FUNCTION);
                    assert(!memcmp(&bw, &bw_copy, sizeof(bw)));
                    assert(!strcmp(path_a, "C:\\"));
                    assert(!memcmp(path_w, L"C:\\", sizeof(path_w)));
                    calls += 2;
                }
    printf("volume_mount: %u A/W direct/dynamic failure calls passed\n", calls);
    return 0;
}
