// Status-only diagnostics. A single process budget prevents repeated failures from flooding logs.
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "vx.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned count;
static int enabled = -1;

void vx_netdiag(const char *api, int64_t result, const void *caller) {
    int host_error = errno;
    DWORD error = vx_get_last_error();
    static const char *const allowed[] = {
        "GetBestRoute", "GetBestInterface", "GetAdaptersAddresses", "GetAdaptersInfo", "GetIfTable",
        "GetIpAddrTable", "GetNetworkParams", "ConvertInterfaceGuidToLuid", "GetIfEntry2",
        "WinHttpGetDefaultProxyConfiguration", "WinHttpGetIEProxyConfigForCurrentUser",
        "WSAStartup", "socket", "WSASocketA", "WSASocketW", "bind", "connect", "setsockopt", "WSAIoctl",
        "getaddrinfo", "MessageBoxA", "MessageBoxW"
    };
    const char *name = NULL;
    for (size_t i = 0; api && i < sizeof allowed / sizeof *allowed; i++)
        if (!strcmp(api, allowed[i])) { name = allowed[i]; break; }
    if (!name) goto done;
    pthread_mutex_lock(&lock);
    if (enabled < 0) {
        const char *e = getenv("VXRT_NET_DIAG");
        enabled = e && !strcmp(e, "1");
    }
    if (enabled && count < 128) {
        unsigned seq = ++count;
        const vx_image *m = vx_image_at((uint64_t)(uintptr_t)caller);
        char module[64];
        size_t n = 0;
        // Never emit a path, control character or unbounded dynamic module name.
        if (m) {
            for (; n < sizeof module - 1 && m->name[n]; n++) {
                unsigned char c = (unsigned char)m->name[n];
                module[n] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') ? (char)c : '_';
            }
            module[n] = 0;
        }
        // MessageBox can block for the whole diagnostic window; record entry, without a fictitious return value.
        int message = !strcmp(name, "MessageBoxA") || !strcmp(name, "MessageBoxW");
        if (message && m && n)
            vx_log("netdiag seq=%u api=%s error=%u errno=%d caller=%s+0x%llx", seq, name, error, host_error,
                   module, (unsigned long long)((uintptr_t)caller - (uintptr_t)m->base));
        else if (message)
            vx_log("netdiag seq=%u api=%s error=%u errno=%d caller=unknown", seq, name, error, host_error);
        else if (m && n)
            vx_log("netdiag seq=%u api=%s result=%lld error=%u errno=%d caller=%s+0x%llx", seq, name,
                   (long long)result, error, host_error, module,
                   (unsigned long long)((uintptr_t)caller - (uintptr_t)m->base));
        else
            vx_log("netdiag seq=%u api=%s result=%lld error=%u errno=%d caller=unknown", seq, name,
                   (long long)result, error, host_error);
    }
    pthread_mutex_unlock(&lock);
done:
    vx_set_last_error(error);
    errno = host_error;
}
