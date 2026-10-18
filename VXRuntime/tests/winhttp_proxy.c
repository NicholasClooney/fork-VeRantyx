// An unavailable WinHTTP configuration must not be reported as a direct connection.
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void) {
    HMODULE dll = LoadLibraryA("WINHTTP.dll");
    CHECK(dll != NULL);
    if (!dll) return 1;
    typedef BOOL (WINAPI *proxy_fn)(WINHTTP_PROXY_INFO *);
    proxy_fn get_proxy = (proxy_fn)GetProcAddress(dll, "WinHttpGetDefaultProxyConfiguration");
    CHECK(get_proxy != NULL);
    if (!get_proxy) return 1;
    struct { DWORD before; WINHTTP_PROXY_INFO info; DWORD after; } guarded, original;
    WCHAR proxy[] = L"sentinel", bypass[] = L"unchanged";
    memset(&guarded, 0xa5, sizeof guarded);
    guarded.info.dwAccessType = 0x12345678;
    guarded.info.lpszProxy = proxy;
    guarded.info.lpszProxyBypass = bypass;
    memcpy(&original, &guarded, sizeof original);
    SetLastError(0x12345678);
    CHECK(get_proxy(&guarded.info) == FALSE);
    CHECK(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
    CHECK(memcmp(&guarded, &original, sizeof guarded) == 0);
    CHECK(memcmp(proxy, L"sentinel", sizeof proxy) == 0);
    CHECK(memcmp(bypass, L"unchanged", sizeof bypass) == 0);
    SetLastError(0);
    CHECK(get_proxy(NULL) == FALSE);
    CHECK(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
    typedef BOOL (WINAPI *ie_proxy_fn)(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *);
    ie_proxy_fn get_ie_proxy = (ie_proxy_fn)GetProcAddress(dll, "WinHttpGetIEProxyConfigForCurrentUser");
    CHECK(get_ie_proxy != NULL);
    if (!get_ie_proxy) return 1;
    struct { DWORD before; WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config; DWORD after; } ie, ie_original;
    WCHAR auto_url[] = L"untouched";
    memset(&ie, 0x5a, sizeof ie);
    ie.config.fAutoDetect = TRUE;
    ie.config.lpszAutoConfigUrl = auto_url;
    ie.config.lpszProxy = proxy;
    ie.config.lpszProxyBypass = bypass;
    memcpy(&ie_original, &ie, sizeof ie_original);
    SetLastError(0x12345678);
    CHECK(get_ie_proxy(&ie.config) == FALSE);
    CHECK(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
    CHECK(memcmp(&ie, &ie_original, sizeof ie) == 0);
    CHECK(memcmp(auto_url, L"untouched", sizeof auto_url) == 0);
    CHECK(memcmp(proxy, L"sentinel", sizeof proxy) == 0);
    CHECK(memcmp(bypass, L"unchanged", sizeof bypass) == 0);
    SetLastError(0);
    CHECK(get_ie_proxy(NULL) == FALSE);
    CHECK(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
    CHECK(FreeLibrary(dll));
    printf("winhttp_proxy: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
