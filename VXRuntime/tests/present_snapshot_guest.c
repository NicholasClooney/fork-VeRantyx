// Synthetic back buffers only. The host controls capture through the private diagnostic socket.
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(h,m,a,b); }
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "present";
    WNDCLASSW wc = {0}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(NULL); wc.lpszClassName=L"SnapshotFixture";
    assert(RegisterClassW(&wc));
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"Synthetic snapshot fixture",WS_OVERLAPPEDWINDOW,0,0,64,48,NULL,NULL,wc.hInstance,NULL);
    assert(hwnd);
    ID3D11Device *device=NULL; ID3D11DeviceContext *context=NULL;
    assert(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&device,NULL,&context)==S_OK);
    IDXGIFactory *factory=NULL; assert(CreateDXGIFactory(&IID_IDXGIFactory,(void**)&factory)==S_OK);
    DXGI_SWAP_CHAIN_DESC desc={0}; desc.BufferDesc.Width=64; desc.BufferDesc.Height=48;
    desc.BufferDesc.Format=!strcmp(mode,"unsupported") ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=1; desc.OutputWindow=hwnd; desc.Windowed=TRUE;
    IDXGISwapChain *chains[2];
    for (int i=0;i<2;i++) {
        assert(IDXGIFactory_CreateSwapChain(factory,(IUnknown*)device,&desc,&chains[i])==S_OK);
        ID3D11Texture2D *buffer=NULL; ID3D11RenderTargetView *view=NULL;
        assert(IDXGISwapChain_GetBuffer(chains[i],0,&IID_ID3D11Texture2D,(void**)&buffer)==S_OK);
        assert(ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)buffer,NULL,&view)==S_OK);
        const float color[4]={0.25f,0.5f,0.75f,1}; ID3D11DeviceContext_ClearRenderTargetView(context,view,color);
        ID3D11RenderTargetView_Release(view); ID3D11Texture2D_Release(buffer);
    }
    puts("snapshot fixture ready"); fflush(stdout);
    Sleep(2000);
    SetLastError(0x1234);
    assert(IDXGISwapChain_Present(chains[0],0,DXGI_PRESENT_TEST)==S_OK);
    assert(GetLastError()==0x1234);
    puts("snapshot fixture TEST complete"); fflush(stdout);
    Sleep(2000);
    if (strcmp(mode,"none")) {
        assert(IDXGISwapChain_Present(chains[0],0,0)==S_OK);
        assert(GetLastError()==0x1234);
        puts("snapshot fixture first present complete"); fflush(stdout);
        Sleep(1000);
        for (int i=1;i<6;i++) assert(IDXGISwapChain_Present(chains[i%2],0,0)==S_OK);
    }
    puts("snapshot fixture presents complete"); fflush(stdout);
    Sleep(2000);
    for(int i=0;i<2;i++) IDXGISwapChain_Release(chains[i]);
    IDXGIFactory_Release(factory); ID3D11DeviceContext_Release(context); ID3D11Device_Release(device); DestroyWindow(hwnd);
    return 0;
}
