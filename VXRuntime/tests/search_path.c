#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef DWORD (WINAPI *search_w)(LPCWSTR,LPCWSTR,LPCWSTR,DWORD,LPWSTR,LPWSTR*);
static WCHAR output[512], saved[512];
static void unchanged(const WCHAR *dirs, const WCHAR *name, const WCHAR *ext, DWORD cap, DWORD error) {
    memset(output, 0x5a, sizeof(output)); memcpy(saved, output, sizeof(output));
    WCHAR *part = (WCHAR *)(uintptr_t)0x1234;
    SetLastError(0xdead);
    assert(SearchPathW(dirs,name,ext,cap,output,&part) == 0);
    assert(GetLastError() == error);
    assert(!memcmp(output,saved,sizeof(output)) && part == (WCHAR *)(uintptr_t)0x1234);
}
int main(void) {
    search_w fn = (search_w)GetProcAddress(GetModuleHandleA("kernel32.dll"),"SearchPathW");
    assert(fn);
    const WCHAR *path = L"C:\\tree\\中文\\数据.bin";
    DWORD len = (DWORD)wcslen(path);
    WCHAR *part = NULL;
    assert(fn(NULL,path,NULL,512,output,&part) == len);
    assert(!wcscmp(output,path) && part == output + len - wcslen(L"数据.bin"));
    assert(SearchPathW(L"C:\\missing;C:\\tree\\中文",L"数据",L".bin",512,output,NULL) == len);
    assert(!wcscmp(output,path));
    assert(SearchPathW(L"C:\\tree\\中文",L"数据.bin",L".wrong",512,output,&part) == len);
    assert(SearchPathW(L"C:\\tree;C:\\second",L"choice",L".txt",512,output,NULL));
    assert(!wcscmp(output,L"C:\\tree\\choice.txt"));
    assert(SearchPathW(L"C:\\tree",L"shadow",L".txt",512,output,NULL));
    assert(!wcscmp(output,L"C:\\tree\\shadow.txt"));
    assert(SetCurrentDirectoryW(L"C:\\tree"));
    assert(SearchPathW(NULL,L"中文\\数据.bin",NULL,512,output,NULL) == len);
    for (DWORD cap = 0; cap <= len; ++cap) {
        memset(output,0x5a,sizeof(output)); memcpy(saved,output,sizeof(output));
        part = (WCHAR *)(uintptr_t)0x1234;
        assert(SearchPathW(NULL,path,NULL,cap,output,&part) == len + 1);
        assert(!memcmp(output,saved,sizeof(output)) && part == (WCHAR *)(uintptr_t)0x1234);
    }
    assert(SearchPathW(NULL,path,NULL,0,NULL,NULL) == len + 1);
    assert(SearchPathW(NULL,path,NULL,len + 1,output,&part) == len);
    assert(part >= output && part < output + len && !wcscmp(part,L"数据.bin"));
    unchanged(L"C:\\tree",L"absent",NULL,512,ERROR_FILE_NOT_FOUND);
    unchanged(NULL,L"choice.txt",NULL,512,ERROR_NOT_SUPPORTED);
    unchanged(NULL,L"\\\\server\\file",NULL,512,ERROR_NOT_SUPPORTED);
    unchanged(L"C:\\tree",NULL,NULL,512,ERROR_INVALID_PARAMETER);
    unchanged(L"C:\\denied;C:\\second",L"choice.txt",NULL,512,ERROR_ACCESS_DENIED);
    unchanged(L"C:\\tree\\blocked;C:\\second",L"choice.txt",NULL,512,ERROR_ACCESS_DENIED);
    SetLastError(0);
    assert(!SearchPathW(NULL,path,NULL,2,NULL,NULL) && GetLastError() == ERROR_INVALID_PARAMETER);
    puts("search_path: explicit/list/Unicode/extension/overlay/permissions/buffer/filepart passed");
    return 0;
}
