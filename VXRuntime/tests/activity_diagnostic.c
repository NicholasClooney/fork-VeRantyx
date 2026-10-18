#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <assert.h>
#include <stdio.h>
static unsigned callbacks, timers;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_APP) { ++callbacks; SetLastError(731); return 123; }
    return DefWindowProcW(h,m,a,b);
}
static void CALLBACK timer(HWND h, UINT m, UINT_PTR id, DWORD now) {
    (void)h;(void)id;(void)now;assert(m==WM_TIMER);++timers;SetLastError(732);
}
static void phase(unsigned n) { printf("activity phase %u\n",n);fflush(stdout);Sleep(3000); }
int main(void) {
    ID3D11Device *dev=NULL;ID3D11DeviceContext *ctx=NULL;
    assert(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&dev,NULL,&ctx)==S_OK);
    WNDCLASSW wc={0};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"SECRET_ACTIVITY_CLASS";
    assert(RegisterClassW(&wc));
    HWND h=CreateWindowExW(0,wc.lpszClassName,L"SECRET_ACTIVITY_TITLE",0,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(h);
    D3D11_BUFFER_DESC desc={0};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer *a=NULL,*b=NULL;assert(ID3D11Device_CreateBuffer(dev,&desc,NULL,&a)==S_OK);
    assert(ID3D11Device_CreateBuffer(dev,&desc,NULL,&b)==S_OK);
    ID3D11DeviceContext_Flush(ctx);ID3D11DeviceContext_Flush(ctx);phase(1);
    MSG m={0};m.hwnd=h;m.message=WM_APP;
    assert(DispatchMessageA(&m)==123 && GetLastError()==731);
    assert(DispatchMessageW(&m)==123 && GetLastError()==731);
    m.hwnd=NULL;m.message=WM_TIMER;m.lParam=(LPARAM)timer;
    assert(DispatchMessageW(&m)==0 && GetLastError()==732);
    m.message=WM_APP;m.lParam=0;SetLastError(733);
    assert(DispatchMessageA(&m)==0 && GetLastError()==733);
    assert(DispatchMessageW(&m)==0 && GetLastError()==733);
    assert(callbacks==2 && timers==1);
    ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)b,(ID3D11Resource*)a);
    ID3D11DeviceContext_Flush(ctx);phase(2);
    ID3D11DeviceContext_Flush(ctx);ID3D11DeviceContext_Flush(ctx);phase(3);
    ID3D11Buffer_Release(a);ID3D11Buffer_Release(b);ID3D11DeviceContext_Release(ctx);ID3D11Device_Release(dev);DestroyWindow(h);
    puts("activity PE passed");return 0;
}
