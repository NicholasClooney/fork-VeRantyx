// DbgHelp's unsupported dump writer must fail without damaging the caller's file.
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void) {
    HMODULE dll = LoadLibraryA("dbghelp.dll");
    CHECK(dll != NULL);
    if (!dll) return 1;
    typedef BOOL (WINAPI *dump_fn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
        PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    dump_fn dump = (dump_fn)GetProcAddress(dll, "MiniDumpWriteDump");
    CHECK(dump != NULL);
    if (!dump) return 1;
    HANDLE file = CreateFileA("minidump-test.tmp", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return 1;
    const char sentinel[] = "preserve existing diagnostic data";
    DWORD count = 0;
    CHECK(WriteFile(file, sentinel, sizeof sentinel, &count, NULL) && count == sizeof sentinel);
    CHECK(SetFilePointer(file, 5, NULL, FILE_BEGIN) == 5);
    SetLastError(0x12345678);
    CHECK(!dump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, NULL, NULL, NULL));
    CHECK(GetLastError() == (DWORD)HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED));
    CHECK(SetFilePointer(file, 0, NULL, FILE_CURRENT) == 5);
    CHECK(GetFileSize(file, NULL) == sizeof sentinel);
    CHECK(SetFilePointer(file, 0, NULL, FILE_BEGIN) == 0);
    char actual[sizeof sentinel];
    CHECK(ReadFile(file, actual, sizeof actual, &count, NULL) && count == sizeof actual);
    CHECK(memcmp(actual, sentinel, sizeof sentinel) == 0);
    CHECK(CloseHandle(file));
    CHECK(DeleteFileA("minidump-test.tmp"));

    // An empty caller-created dump must remain empty too; no misleading dump header.
    file = CreateFileA("minidump-test.tmp", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return 1;
    SetLastError(0);
    CHECK(!dump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithFullMemory, NULL, NULL, NULL));
    CHECK(GetLastError() == (DWORD)HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED));
    CHECK(GetFileSize(file, NULL) == 0);
    CHECK(CloseHandle(file));
    CHECK(DeleteFileA("minidump-test.tmp"));
    CHECK(FreeLibrary(dll));
    printf("minidump: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
