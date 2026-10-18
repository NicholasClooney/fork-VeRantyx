// Unsupported routing queries must not invent interfaces or change caller state.
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void) {
    HMODULE dll = LoadLibraryA("IPHLPAPI.DLL");
    CHECK(dll != NULL);
    if (!dll) return 1;
    typedef DWORD (WINAPI *route_fn)(DWORD, DWORD, PMIB_IPFORWARDROW);
    route_fn best_route = (route_fn)GetProcAddress(dll, "GetBestRoute");
    CHECK(best_route != NULL);
    if (!best_route) return 1;
    // Network-order IPv4 bytes on x86-64: unspecified, loopback, documentation range, broadcast.
    DWORD addresses[] = { 0, 0x0100007f, 0x010200c0, 0xffffffff };
    struct { DWORD before; MIB_IPFORWARDROW route; DWORD after; } guarded, original;
    for (unsigned d = 0; d < sizeof addresses / sizeof *addresses; d++)
        for (unsigned s = 0; s < sizeof addresses / sizeof *addresses; s++) {
            memset(&guarded, 0xa5, sizeof guarded);
            memcpy(&original, &guarded, sizeof original);
            SetLastError(0x12345678);
            CHECK(best_route(addresses[d], addresses[s], &guarded.route) == ERROR_NOT_SUPPORTED);
            CHECK(GetLastError() == 0x12345678);
            CHECK(memcmp(&guarded, &original, sizeof guarded) == 0);
            SetLastError(0x87654321);
            CHECK(best_route(addresses[d], addresses[s], NULL) == ERROR_NOT_SUPPORTED);
            CHECK(GetLastError() == 0x87654321);
        }
    CHECK(FreeLibrary(dll));
    printf("best_route: %s (32 calls)\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
