// Command Clips: a running program's windows as text, and actions on them as messages, without looking at the screen
// (docs: VXRuntime/CLIPS.md). VXRT_CONTROL=<path> opens a Unix socket there (mode 0600, removed at exit); each line
// sent is one request, each answered with one line of JSON:
//   state                      the windows: class, title, client size and origin on the screen, the text drawn in each
//                              and where; the active window, the focus; whether the program is idle
//   windows                    the same without the text (what a screenshot-only agent may know: tools/vxrt_screen.py)
//   snapshot <hwnd> <path>     the window's client area as a PNG file
//   click <hwnd> <x> <y>       a left click at client coordinates (hwnd 0: the active window, else the first visible one)
//   click-text <hwnd> <text>   a click on the middle of the first drawn text that contains <text> (hwnd 0: any window)
//   key <hwnd> <vk>            a key pressed and let go (a virtual-key code, decimal or 0x..; no WM_CHAR: see type)
//   type <hwnd> <text>         the characters of <text> (UTF-8) as WM_CHAR
//   close <hwnd>               WM_CLOSE
//   wait-idle [ms]             answers once the program is idle (user32.c, vx_ui_idle: the actions so far handled and
//                              painted), or after ms (default 2000) with "idle": false
// VXRT_CONTROL_LOG=<file>: each request appended with the milliseconds since the socket opened (how an agent worked).
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "ui.h"
#include "renderdiag.h"
#include "cpudiag.h"

static char g_path[104];
static FILE *g_log;
static double g_t0;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void sleep_ms(int ms) { struct timespec t = { 0, ms * 1000000L }; nanosleep(&t, NULL); }

static void reply(int fd, const char *s) {
    size_t n = strlen(s);
    while (n) { ssize_t k = write(fd, s, n); if (k <= 0) return; s += k; n -= (size_t)k; }
    if (write(fd, "\n", 1) < 0) return;
}
static void reply_ok(int fd, HANDLE h) { char b[96]; snprintf(b, sizeof b, "{\"ok\":true,\"hwnd\":%lld}", (long long)(intptr_t)h); reply(fd, b); }
static void reply_err(int fd, const char *what) {
    char b[256]; snprintf(b, sizeof b, "{\"ok\":false,\"error\":\"%s\"}", what); reply(fd, b);
}
// The next word of *s (spaces skipped); the rest stays in *s.
static char *word(char **s) {
    while (**s == ' ') (*s)++;
    if (!**s) return NULL;
    char *w = *s;
    while (**s && **s != ' ') (*s)++;
    if (**s) *(*s)++ = 0;
    return w;
}
static HANDLE window_arg(char **s) {
    char *w = word(s);
    return vx_ui_window(w ? (HANDLE)(intptr_t)strtoll(w, NULL, 0) : NULL);
}

static void request(int fd, char *line) {
    if (g_log) { pthread_mutex_lock(&g_log_lock); fprintf(g_log, "%.1f %s\n", now_ms() - g_t0, line); fflush(g_log); pthread_mutex_unlock(&g_log_lock); }
    char *s = line, *cmd = word(&s);
    if (!cmd) { reply_err(fd, "empty request"); return; }
    if (!strcmp(cmd, "state") || !strcmp(cmd, "windows")) { char *j = vx_ui_state(cmd[0] == 's'); reply(fd, j); free(j); return; }
    if (!strcmp(cmd, "wait-idle")) {
        char *a = word(&s);
        int ms = a ? atoi(a) : 2000, stable = 0;
        double t0 = now_ms();
        for (;;) {
            stable = vx_ui_idle() ? stable + 1 : 0;                       // (twice in a row: a message posted between
            if (stable >= 2) break;                                       //  two threads' checks is not missed)
            if (now_ms() - t0 >= ms) break;
            sleep_ms(1);
        }
        char b[96]; snprintf(b, sizeof b, "{\"ok\":true,\"idle\":%s,\"ms\":%.1f}", stable >= 2 ? "true" : "false", now_ms() - t0);
        reply(fd, b);
        return;
    }
    if (!strcmp(cmd, "click-text")) {
        char *w = word(&s);
        while (*s == ' ') s++;
        HANDLE in; POINT at;
        if (!w || !*s) { reply_err(fd, "click-text <hwnd> <text>"); return; }
        if (!vx_ui_find_text((HANDLE)(intptr_t)strtoll(w, NULL, 0), s, &in, &at)) { reply_err(fd, "no drawn text contains it"); return; }
        vx_ui_click(in, (int)at.x, (int)at.y);
        char b[128]; snprintf(b, sizeof b, "{\"ok\":true,\"hwnd\":%lld,\"at\":[%d,%d]}", (long long)(intptr_t)in, (int)at.x, (int)at.y);
        reply(fd, b);
        return;
    }
    HANDLE h = window_arg(&s);
    if (!h) { reply_err(fd, "no such window"); return; }
    if (!strcmp(cmd, "click")) {
        char *x = word(&s), *y = word(&s);
        if (!x || !y) { reply_err(fd, "click <hwnd> <x> <y>"); return; }
        vx_ui_click(h, atoi(x), atoi(y));
    } else if (!strcmp(cmd, "key")) {
        char *k = word(&s);
        if (!k) { reply_err(fd, "key <hwnd> <vk>"); return; }
        vx_ui_key(h, (int)strtol(k, NULL, 0));
    } else if (!strcmp(cmd, "type")) {
        while (*s == ' ') s++;
        vx_ui_type(h, s);
    } else if (!strcmp(cmd, "snapshot")) {
        while (*s == ' ') s++;
        if (!*s || !vx_ui_snapshot(h, s)) { reply_err(fd, "snapshot <hwnd> <path>: not written"); return; }
    } else if (!strcmp(cmd, "close")) vx_window_post(h, WM_CLOSE, 0, 0);
    else { reply_err(fd, "unknown request (state, windows, snapshot, click, click-text, key, type, close, wait-idle)"); return; }
    reply_ok(fd, h);
}

static void *connection(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[8192]; size_t n = 0;
    for (;;) {
        ssize_t k = read(fd, buf + n, sizeof buf - 1 - n);
        if (k <= 0) break;
        n += (size_t)k;
        char *nl;
        while ((nl = memchr(buf, '\n', n))) {
            *nl = 0;
            if (nl > buf && nl[-1] == '\r') nl[-1] = 0;
            request(fd, buf);
            n -= (size_t)(nl + 1 - buf);
            memmove(buf, nl + 1, n);
        }
        if (n == sizeof buf - 1) n = 0;                                    // (a line too long: dropped)
    }
    close(fd);
    return NULL;
}
static void *serve(void *arg) {
    int ls = (int)(intptr_t)arg;
    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; break; }
        pthread_t t;
        if (pthread_create(&t, NULL, connection, (void *)(intptr_t)fd) == 0) pthread_detach(t); else close(fd);
    }
    return NULL;
}
static void remove_socket(void) { if (g_path[0]) unlink(g_path); }

void vx_control_start(const char *path) {
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof a.sun_path) { vx_log("control: the socket path is too long: %s", path); return; }
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0) return;
    unlink(path);
    mode_t old = umask(077);                                              // (only this user may connect)
    int ok = bind(ls, (struct sockaddr *)&a, sizeof a) == 0 && listen(ls, 4) == 0;
    umask(old);
    if (!ok) { vx_log("control: cannot listen at %s: %s", path, strerror(errno)); close(ls); return; }
    snprintf(g_path, sizeof g_path, "%s", path);
    atexit(remove_socket);
    g_t0 = now_ms();
    if (getenv("VXRT_CONTROL_LOG")) g_log = fopen(getenv("VXRT_CONTROL_LOG"), "a");
    pthread_t t;
    if (pthread_create(&t, NULL, serve, (void *)(intptr_t)ls) == 0) pthread_detach(t);
    vx_log("control: Command Clips at %s", path);
}


// A separate protocol keeps the full Clip action/text surface out of diagnostics.
static char g_meta_path[104];
static dev_t g_meta_dev;
static ino_t g_meta_ino;
static int g_cpu_mode;
static int g_meta_mode; // 1 windows, 2 render, 3 both
static char *diagnostic_snapshot(void) {
    vx_cpudiag_start();
    char *windows = g_meta_mode != 2 ? vx_ui_metadata() : NULL;
    if (g_meta_mode == 1) return windows;
    char *render = vx_renderdiag_snapshot();
    char *out = calloc(1, 4096);
    if (out && render) {
        if (windows) { windows[strlen(windows)-1] = 0; snprintf(out, 4096, "%s,\"render\":%s}", windows, render); }
        else snprintf(out, 4096, "{\"schema_version\":1,\"status\":\"ok\",\"pid\":%d,\"render\":%s}", (int)getpid(), render);
    } else { free(out); out = NULL; }
    free(windows); free(render);
    return out;
}
static char *cpu_snapshot(void) {
    char *cpu = vx_cpudiag_snapshot(), *out = calloc(1, 4096);
    if (cpu && out) snprintf(out, 4096, "{\"status\":\"ok\",\"pid\":%d,\"cpu\":%s}", (int)getpid(), cpu);
    else { free(out); out = NULL; }
    free(cpu);
    return out;
}
static void metadata_cleanup(void) {
    struct stat st;
    if (g_meta_path[0] && !lstat(g_meta_path, &st) && S_ISSOCK(st.st_mode) &&
        st.st_dev == g_meta_dev && st.st_ino == g_meta_ino) unlink(g_meta_path);
}
static int metadata_ready(int fd, short events, double deadline) {
    for (;;) {
        double remaining = deadline - now_ms();
        if (remaining <= 0) return 0;
        struct pollfd p = { .fd = fd, .events = events };
        int n = poll(&p, 1, (int)remaining + 1);
        if (n < 0 && errno == EINTR) continue;
        return n > 0 && (p.revents & events);
    }
}
static void *metadata_serve(void *arg) {
    int ls = (int)(intptr_t)arg;
    for (unsigned attempt = 0; attempt < (g_cpu_mode ? 4u : 3u); ++attempt) {
        int fd;
        do { fd = accept(ls, NULL, NULL); } while (fd < 0 && errno == EINTR);
        if (fd < 0) break;
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) || fcntl(fd, F_SETFL, O_NONBLOCK)) { close(fd); continue; }
        int yes = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
        double deadline = now_ms() + 2000;
        char command[64]; size_t n = 0; int complete = 0;
        while (n < sizeof command - 1 && metadata_ready(fd, POLLIN, deadline)) {
            ssize_t k = read(fd, command + n, 1);
            if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (k != 1) break;
            if (command[n++] == '\n') { complete = 1; break; }
        }
        command[n] = 0;
        char *state = complete && !strcmp(command, "metadata\n") ? diagnostic_snapshot() :
                      complete && g_cpu_mode && !strcmp(command, "cpu\n") ? cpu_snapshot() : NULL;
        const char *response = state ? state : "{\"status\":\"rejected\"}";
        char wire[4096]; int len = snprintf(wire, sizeof wire, "%s\n", response);
        free(state);
        size_t sent = 0;
        while (len > 0 && (size_t)len < sizeof wire && sent < (size_t)len && metadata_ready(fd, POLLOUT, deadline)) {
            ssize_t k = write(fd, wire + sent, (size_t)len - sent);
            if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (k <= 0) break;
            sent += (size_t)k;
        }
        close(fd);
    }
    close(ls);
    metadata_cleanup();
    return NULL;
}
void vx_control_metadata_start(const char *exe) {
    const char *mode = getenv("VXRT_CONTROL_DIAG"), *target = getenv("VXRT_CONTROL_TARGET");
    const char *path = getenv("VXRT_CONTROL_DIAG_SOCKET");
    if (!mode || !target || !path || getenv("VXRT_CHILD")) return;
    int selected = !strcmp(mode, "metadata") ? 1 : !strcmp(mode, "render") ? 2 : !strcmp(mode, "metadata_render") ? 3 : 0;
    if (!selected) return;
    char *real = realpath(target, NULL);
    int matches = real && !strcmp(real, exe);
    free(real);
    if (!matches || strlen(path) >= sizeof g_meta_path || strncmp(path, "/private/tmp/vxclip-", 20)) return;
    char parent[104]; snprintf(parent, sizeof parent, "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash || strcmp(slash, "/control.sock")) return;
    *slash = 0;
    struct stat st;
    char *canonical = realpath(parent, NULL);
    int valid = canonical && !strcmp(canonical, parent);
    free(canonical);
    if (!valid || lstat(parent, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) return;
    if (!lstat(path, &st) || errno != ENOENT) return;
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0) return;
    if (fcntl(ls, F_SETFD, FD_CLOEXEC)) { close(ls); return; }
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path, sizeof address.sun_path, "%s", path);
    // The private directory protects the short interval before chmod.
    if (bind(ls, (struct sockaddr *)&address, sizeof address)) { close(ls); return; }
    if (lstat(path, &st)) { close(ls); return; }
    snprintf(g_meta_path, sizeof g_meta_path, "%s", path);
    g_meta_dev = st.st_dev; g_meta_ino = st.st_ino;
    if (chmod(path, 0600) || listen(ls, 3)) { close(ls); metadata_cleanup(); return; }
    atexit(metadata_cleanup);
    g_meta_mode = selected;
    g_cpu_mode = getenv("VXRT_CPU_DIAG") && !strcmp(getenv("VXRT_CPU_DIAG"), "1");
    if (g_cpu_mode) vx_cpudiag_enable();
    if (selected & 2) vx_renderdiag_enable();
    pthread_t thread;
    if (pthread_create(&thread, NULL, metadata_serve, (void *)(intptr_t)ls)) {
        close(ls); metadata_cleanup(); return;
    }
    pthread_detach(thread);
}
