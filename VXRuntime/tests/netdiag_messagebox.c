// Use the actual entry wrappers with a non-UI backend so this test cannot open a modal dialog.
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include "../src/user32.c"
static DWORD last;
static int diagnostics, texts, boxes;
DWORD vx_get_last_error(void) { return last; }
void vx_set_last_error(DWORD e) { last = e; }
const vx_image *vx_image_at(uint64_t pc) { (void)pc; return NULL; }
char *vx_utf8_dup(const WCHAR *w) { (void)w; return strdup("SECRET"); }
void vx_log(const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (strstr(buf, "netdiag ")) {
        diagnostics++;
        assert(boxes == diagnostics - 1);
        assert(strstr(buf, diagnostics == 1 ? "api=MessageBoxA " : "api=MessageBoxW "));
        assert(!strstr(buf, "SECRET") && !strstr(buf, "result="));
        assert(strstr(buf, "caller=unknown"));
        errno = ERANGE; last = 999;
    } else {
        assert(!strcmp(buf, "MessageBox: SECRET: SECRET"));
        assert(last == 456 && errno == 7);  // Diagnostic has restored entry state before the original text log.
        texts++;
    }
}
int vx_cocoa_message_box(const char *text, const char *caption, uint32_t type) {
    assert(!strcmp(text, "SECRET") && !strcmp(caption, "SECRET") && type == 0);
    boxes++; assert(diagnostics == boxes && texts == boxes);
    errno = 8; last = 123;
    return 2;
}
int main(void) {
    last = 456; errno = 7;
    assert(MessageBoxA(NULL, "SECRET", "SECRET", 0) == 2);
    assert(last == 123 && errno == 8);
    last = 456; errno = 7;
    const WCHAR text[] = {'S', 0};
    assert(MessageBoxW(NULL, text, text, 0) == 2);
    assert(last == 123 && errno == 8);
    assert(diagnostics == 2 && texts == 2 && boxes == 2);
    puts("netdiag_messagebox: PASS");
}
