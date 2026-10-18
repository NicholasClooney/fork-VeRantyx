// A normal MinGW program: C runtime startup, argv, printf, malloc, a static constructor.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <math.h>
#include <errno.h>
static int test_strerror_s(void) {
    const char *dlls[] = { "ucrtbase.dll", "api-ms-win-crt-runtime-l1-1-0.dll" };
    const struct { int error; const char *message; } cases[] = {
        { 0, "No error" }, { 2, "No such file or directory" }, { 13, "Permission denied" },
        { 22, "Invalid argument" }, { 34, "Result too large" }, { 36, "Resource deadlock avoided" },
        { 38, "Filename too long" }, { 42, "Illegal byte sequence" }, { 107, "Connection refused" },
        { -1, "Unknown error" }, { 9999, "Unknown error" },
    };
    for (unsigned m = 0; m < sizeof dlls / sizeof *dlls; m++) {
        HMODULE module = LoadLibraryA(dlls[m]);
        int (__cdecl *describe)(char *, size_t, int) = (void *)GetProcAddress(module, "strerror_s");
        char *(__cdecl *plain)(int) = (void *)GetProcAddress(module, "strerror");
        int *(__cdecl *crt_errno)(void) = (void *)GetProcAddress(module, "_errno");
        if (!describe || !plain || !crt_errno) return 13;
        for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
            size_t length = strlen(cases[c].message);
            size_t sizes[] = { 64, length + 1, length, 4, 1 };
            if (strcmp(plain(cases[c].error), cases[c].message)) return 14;
            for (unsigned s = 0; s < sizeof sizes / sizeof *sizes; s++) {
                unsigned char guarded[66], expected[66];
                memset(guarded, 0xa5, sizeof guarded);
                memset(expected, 0xa5, sizeof expected);
                size_t n = length < sizes[s] ? length : sizes[s] - 1;
                memcpy(expected + 1, cases[c].message, n);
                expected[n + 1] = 0;
                *crt_errno() = 123;
                int result = describe((char *)guarded + 1, sizes[s], cases[c].error);
                if (result || *crt_errno() != 123 || memcmp(guarded, expected, sizeof guarded)) {
                    printf("strerror_s: FAIL module=%s case=%u size=%u\n", dlls[m], c, (unsigned)sizes[s]);
                    return 15;
                }
            }
        }
        char guard[3] = { 'a', 'b', 'c' };
        *crt_errno() = 0;
        if (describe(guard + 1, 0, 22) != EINVAL || *crt_errno() != EINVAL || memcmp(guard, "abc", 3)) return 16;
        *crt_errno() = 0;
        if (describe(NULL, 16, 22) != EINVAL || *crt_errno() != EINVAL) return 17;
        *crt_errno() = 0;
        if (describe(NULL, 0, 22) != EINVAL || *crt_errno() != EINVAL) return 18;
        FreeLibrary(module);
    }
    printf("strerror_s: PASS (110 buffer cases, 6 invalid parameter cases)\n");
    return 0;
}
static int test_wtof(void) {
    const char *dlls[] = { "ucrtbase.dll", "api-ms-win-crt-convert-l1-1-0.dll" };
    const struct { const wchar_t *src; double expected; int error; } cases[] = {
        { L"42.5", 42.5, 123 }, { L"-3.25", -3.25, 123 },
        { L" \t+1.25e2tail", 125.0, 123 }, { L".5E-2", 0.005, 123 },
        { L"not12", 0.0, 123 }, { L"", 0.0, 123 }, { L"+", 0.0, 123 },
        { L"12.5rest", 12.5, 123 }, { L"0", 0.0, 123 }, { L"-0", -0.0, 123 },
        { L"1e9999", HUGE_VAL, ERANGE }, { L"-1e9999", -HUGE_VAL, ERANGE },
        { L"1e-9999", 0.0, ERANGE }, { L"-1e-9999", -0.0, ERANGE },
        { L"12\x4e2d", 12.0, 123 }, { L"\x4e2d" L"12", 0.0, 123 },
        { NULL, 0.0, EINVAL },
    };
    for (unsigned m = 0; m < sizeof dlls / sizeof *dlls; m++) {
        HMODULE module = LoadLibraryA(dlls[m]);
        double (__cdecl *convert)(const wchar_t *) = (void *)GetProcAddress(module, "_wtof");
        int *(__cdecl *crt_errno)(void) = (void *)GetProcAddress(module, "_errno");
        if (!convert || !crt_errno) return 10;
        for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
            *crt_errno() = 123;
            double actual = convert(cases[c].src);
            int error = *crt_errno();
            if (actual != cases[c].expected || !!signbit(actual) != !!signbit(cases[c].expected) || error != cases[c].error) {
                printf("_wtof: FAIL module=%s case=%u value=%.17g errno=%d\n", dlls[m], c, actual, error);
                return 11;
            }
        }
        wchar_t long_number[260];
        for (unsigned i = 0; i < 250; i++) long_number[i] = '0';
        long_number[250] = '1'; long_number[251] = '.'; long_number[252] = '5'; long_number[253] = 0;
        *crt_errno() = 123;
        if (convert(long_number) != 1.5 || *crt_errno() != 123) return 12;
        FreeLibrary(module);
    }
    printf("_wtof: PASS (36 cases)\n");
    return 0;
}
static int test_wcsncpy(void) {
    const char *dlls[] = { "ucrtbase.dll", "api-ms-win-crt-string-l1-1-0.dll" };
    const struct { wchar_t src[6]; size_t n; wchar_t expected[6]; } cases[] = {
        { { 'A', 0 }, 0, { 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa } },
        { { 0 }, 4, { 0, 0, 0, 0, 0xaaaa, 0xaaaa } },
        { { 'A', 'B', 0 }, 5, { 'A', 'B', 0, 0, 0, 0xaaaa } },
        { { 'A', 'B', 0 }, 2, { 'A', 'B', 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa } },
        { { 'A', 'B', 'C', 0 }, 1, { 'A', 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa } },
        { { 0x4e2d, 0xd83d, 0xde00, 0xd800, 0 }, 6, { 0x4e2d, 0xd83d, 0xde00, 0xd800, 0, 0 } },
        { { 0xd83d, 0xde00, 0 }, 1, { 0xd83d, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa } },
    };
    if (sizeof(wchar_t) != 2) return 7;
    for (unsigned m = 0; m < sizeof dlls / sizeof *dlls; m++) {
        HMODULE module = LoadLibraryA(dlls[m]);
        wchar_t *(__cdecl *copy)(wchar_t *, const wchar_t *, size_t) = (void *)GetProcAddress(module, "wcsncpy");
        if (!copy) return 8;
        for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
            wchar_t guarded[8], src[6];
            for (unsigned i = 0; i < 8; i++) guarded[i] = 0xaaaa;
            memcpy(src, cases[c].src, sizeof src);
            if (copy(guarded + 1, src, cases[c].n) != guarded + 1 ||
                guarded[0] != 0xaaaa || guarded[7] != 0xaaaa ||
                memcmp(guarded + 1, cases[c].expected, sizeof cases[c].expected) ||
                memcmp(src, cases[c].src, sizeof src)) {
                printf("wcsncpy: FAIL module=%s case=%u\n", dlls[m], c);
                return 9;
            }
        }
        FreeLibrary(module);
    }
    printf("wcsncpy: PASS (14 cases)\n");
    return 0;
}
static int constructed;
__attribute__((constructor)) static void init(void) { constructed = 42; }
int main(int argc, char **argv) {
    int wide_result = test_wcsncpy();
    if (wide_result) return wide_result;
    int float_result = test_wtof();
    if (float_result) return float_result;
    int error_result = test_strerror_s();
    if (error_result) return error_result;
    const char *dlls[] = { "ucrtbase.dll", "api-ms-win-crt-heap-l1-1-0.dll" };
    for (unsigned i = 0; i < sizeof dlls / sizeof *dlls; i++) {
        HMODULE module = LoadLibraryA(dlls[i]);
        intptr_t (__cdecl *get_heap)(void) = (void *)GetProcAddress(module, "_get_heap_handle");
        if (!get_heap) return 2;
        HANDLE heap = (HANDLE)get_heap();
        if (!heap || heap != GetProcessHeap() || (HANDLE)get_heap() != heap) return 3;
        unsigned char *p = HeapAlloc(heap, HEAP_ZERO_MEMORY, 64);
        if (!p || HeapSize(heap, 0, p) < 64) return 4;
        for (int j = 0; j < 64; j++) if (p[j]) return 5;
        if (!HeapFree(heap, 0, p)) return 6;
        FreeLibrary(module);
    }
    char *buf = malloc(64);
    snprintf(buf, 64, "argc=%d argv[0]=%s", argc, argc > 0 ? argv[0] : "?");
    printf("hello from the C runtime: %s, constructor=%d, pi=%.3f, hex=%#x\n", buf, constructed, 3.14159, 255);
    free(buf);
    return constructed == 42 ? 0 : 1;
}
