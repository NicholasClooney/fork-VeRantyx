// Native helper tests deliberately make the logger and image resolver disturb both error slots.
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vx.h"
static __thread DWORD last;
static unsigned lines;
static vx_image image;
DWORD vx_get_last_error(void) { return last; }
void vx_set_last_error(DWORD e) { last = e; }
const vx_image *vx_image_at(uint64_t pc) {
    errno = EIO; last = 99;
    return pc == 0x1234 ? &image : NULL;
}
void vx_log(const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    lines++;
    unsigned seq = 0; assert(sscanf(buf, "netdiag seq=%u", &seq) == 1 && seq == lines);
    assert(strstr(buf, "error=456 errno=7"));
    assert(!strstr(buf, "SECRET") && !strstr(buf, "0xdeadbeef") && !strchr(buf, '\n'));
    if (strstr(buf, "api=GetBestRoute")) assert(strstr(buf, "caller=bad___module.dll+0x234"));
    else assert(strstr(buf, "caller=unknown"));
    if (strstr(buf, "api=MessageBox")) assert(!strstr(buf, "result="));
    else assert(strstr(buf, "result=-1"));
    errno = ERANGE; last = 123;
}
static void record(const char *api, const void *caller) {
    last = 456; errno = 7;
    vx_netdiag(api, -1, caller);
    assert(last == 456 && errno == 7);
}
static void *run(void *unused) {
    (void)unused;
    for (int i = 0; i < 100; i++) record("socket", (void *)0xdeadbeef);
    return NULL;
}
int main(void) {
    strcpy(image.name, "bad/\n module.dll"); image.base = (void *)0x1000;
    record("SECRET https://SECRET token=SECRET", (void *)0xdeadbeef);
    record(NULL, NULL);
    assert(lines == 0);
    record("GetBestRoute", (void *)0x1234);
    record("MessageBoxA", NULL);
    record("MessageBoxW", NULL);
    pthread_t threads[8];
    for (int i = 0; i < 8; i++) assert(!pthread_create(&threads[i], NULL, run, NULL));
    for (int i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
    const char *e = getenv("VXRT_NET_DIAG");
    assert(lines == ((e && !strcmp(e, "1")) ? 128 : 0));
    puts("netdiag_host: PASS");
}
