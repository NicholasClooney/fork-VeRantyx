// Exercise actual Windows exports without DNS, network traffic, proxy reads or a modal UI.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
#define UNSUP(call, code) do { SetLastError(456); CHECK((call) == (code)); CHECK(GetLastError() == 456); } while (0)
int main(void) {
    char payload[1024], original[1024]; memset(payload, 0, sizeof payload);
    strcpy(payload, "SECRET https://SECRET.invalid/ token=SECRET"); memcpy(original, payload, sizeof payload);
    ULONG size = sizeof payload; DWORD index = 42;
    UNSUP(GetBestRoute(0, 0, (void *)payload), 50);
    UNSUP(GetBestInterface(0, &index), 50); CHECK(index == 42);
    UNSUP(GetAdaptersAddresses(0, 0x80000000u, NULL, (void *)payload, &size), 50);
    UNSUP(GetAdaptersInfo((void *)payload, &size), 50);
    UNSUP(GetIfTable((void *)payload, &size, FALSE), 50);
    UNSUP(GetIpAddrTable((void *)payload, &size, FALSE), 50);
    UNSUP(GetNetworkParams((void *)payload, &size), 50);
    typedef DWORD (WINAPI *two_fn)(const void *, void *);
    typedef DWORD (WINAPI *one_fn)(void *);
    HMODULE ip = LoadLibraryA("iphlpapi.dll");
    two_fn guid = (two_fn)GetProcAddress(ip, "ConvertInterfaceGuidToLuid");
    one_fn entry = (one_fn)GetProcAddress(ip, "GetIfEntry2"); CHECK(guid && entry);
    if (guid && entry) { UNSUP(guid(payload, payload), 2); UNSUP(entry(payload), 2); }
    CHECK(!WinHttpGetDefaultProxyConfiguration((void *)payload)); CHECK(GetLastError() == 120);
    CHECK(!WinHttpGetIEProxyConfigForCurrentUser((void *)payload)); CHECK(GetLastError() == 120);
    CHECK(size == sizeof payload && !memcmp(payload, original, sizeof payload));
    WSADATA data; CHECK(!WSAStartup(0x202, &data));
    CHECK(socket(-1, SOCK_STREAM, 0) == INVALID_SOCKET);
    CHECK(WSASocketA(-1, SOCK_STREAM, 0, NULL, 0, 0) == INVALID_SOCKET);
    CHECK(WSASocketW(-1, SOCK_STREAM, 0, NULL, 0, 0) == INVALID_SOCKET);
    struct sockaddr_in addr = {0}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(INVALID_SOCKET, (void *)&addr, sizeof addr) == SOCKET_ERROR); CHECK(WSAGetLastError() == 10038);
    CHECK(connect(INVALID_SOCKET, (void *)&addr, sizeof addr) == SOCKET_ERROR); CHECK(WSAGetLastError() == 10038);
    CHECK(setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_REUSEADDR, payload, 4) == SOCKET_ERROR);
    DWORD got = 42;
    CHECK(WSAIoctl(INVALID_SOCKET, 0xffffffff, payload, sizeof payload, payload, sizeof payload, &got, NULL, NULL) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == 10045);
    struct addrinfo hints = {0}, *out = NULL; hints.ai_flags = AI_NUMERICHOST;
    CHECK(getaddrinfo("SECRET.invalid", NULL, &hints, &out) == 11001); CHECK(out == NULL);
    CHECK(!WSACleanup());
    printf("netdiag_api: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
