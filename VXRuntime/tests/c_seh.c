// C SEH must unwind through __C_specific_handler using the Windows entry-point ABI.
__declspec(dllimport) void __stdcall RaiseException(unsigned long, unsigned long, unsigned long, const unsigned long long *);
__declspec(dllimport) int puts(const char *);
unsigned long _exception_code(void);

static volatile int stage, filtered, finalized, caught, resumed, searched;
static int filter(unsigned long actual, unsigned long expected) {
    filtered++;
    return actual == expected ? 1 : 0;
}
__declspec(noinline) static void raise_with_finally(unsigned long code) {
    __try {
        stage = 1;
        RaiseException(code, 0, 0, 0);
        stage = -1;
    } __finally {
        finalized++;
        stage = stage == 1 ? 2 : -2;
    }
}
__declspec(noinline) static int test_unwind(unsigned long code) {
    stage = 0;
    __try {
        raise_with_finally(code);
        return 0;
    } __except(filter(_exception_code(), code)) {
        caught++;
        if (stage != 2) return 0;
        stage = 3;
    }
    return stage == 3;
}
__declspec(noinline) static int test_search(unsigned long code) {
    __try {
        __try {
            RaiseException(code, 0, 0, 0);
            return 0;
        } __except((searched++, 0)) {
            return 0;
        }
    } __except(filter(_exception_code(), code)) {
        return 1;
    }
    return 0;
}
__declspec(noinline) static int test_continue(unsigned long code) {
    __try {
        RaiseException(code, 0, 0, 0);
        resumed++;
    } __except(_exception_code() == code ? -1 : 0) {
        return 0;
    }
    return 1;
}
int main(void) {
    const unsigned long codes[] = { 0xe0001234, 0x406d1388 };
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        unsigned long code = codes[i % 2];
        ok &= test_unwind(code);
        ok &= test_search(code);
        ok &= test_continue(code);
    }
    ok &= finalized == 8 && caught == 8 && filtered == 16 && resumed == 8 && searched == 8;
    puts(ok ? "c_seh: PASS (target unwind, finally, search, continue; two exception codes)" : "c_seh: FAIL");
    return ok ? 0 : 1;
}
