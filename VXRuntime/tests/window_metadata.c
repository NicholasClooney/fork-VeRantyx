#include <windows.h>
#include <assert.h>
#include <stdlib.h>
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w,m,a,b); }
int main(int argc, char **argv) {
    WNDCLASSW cls = {0};
    cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"SECRET_CUSTOM_CLASS_91827";
    assert(RegisterClassW(&cls));
    HWND h=CreateWindowExW(0,cls.lpszClassName,L"SECRET_TITLE_91827",WS_OVERLAPPEDWINDOW,0,0,300,200,NULL,NULL,cls.hInstance,NULL);
    assert(h);
    HWND e=CreateWindowExW(0,L"EDIT",L"SECRET_PASSWORD_91827",WS_CHILD|ES_PASSWORD,0,0,100,20,h,NULL,cls.hInstance,NULL);
    assert(e);
    Sleep(argc>1 ? (DWORD)atoi(argv[1]) : 15000);
    DestroyWindow(h);
    return 0;
}
