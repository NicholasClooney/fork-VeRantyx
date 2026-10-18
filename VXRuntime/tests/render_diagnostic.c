#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <assert.h>
#include <stdio.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM a,LPARAM b) { return DefWindowProcW(h,m,a,b); }
int main(void) {
    D3D_FEATURE_LEVEL invalid=D3D_FEATURE_LEVEL_11_1;
    assert(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,NULL,NULL,NULL)==S_FALSE);
    ID3D11Device *device=NULL;ID3D11DeviceContext *context=NULL;
    assert(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&device,NULL,&context)==S_OK);
    WNDCLASSW wc={0};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"SECRET_RENDER_CLASS_749";
    assert(RegisterClassW(&wc));
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"SECRET_RENDER_TITLE_749",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,160,120,NULL,NULL,wc.hInstance,NULL);assert(hwnd);
    IDXGIFactory2 *factory=NULL;assert(CreateDXGIFactory1(&IID_IDXGIFactory2,(void**)&factory)==S_OK);
    DXGI_SWAP_CHAIN_DESC desc={0};desc.BufferDesc.Width=160;desc.BufferDesc.Height=120;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain *chain=NULL;
    assert(IDXGIFactory2_CreateSwapChain(factory,(IUnknown*)device,&desc,&chain)==E_INVALIDARG);
    desc.OutputWindow=hwnd;
    assert(IDXGIFactory2_CreateSwapChain(factory,(IUnknown*)device,&desc,&chain)==S_OK);
    assert(IDXGISwapChain_Present(chain,0,DXGI_PRESENT_TEST)==S_OK);
    IDXGISwapChain1 *one=NULL;assert(IDXGISwapChain_QueryInterface(chain,&IID_IDXGISwapChain1,(void**)&one)==S_OK);
    DXGI_PRESENT_PARAMETERS params={0};assert(IDXGISwapChain1_Present1(one,0,DXGI_PRESENT_TEST,&params)==S_OK);
    assert(IDXGISwapChain_Present(chain,0,0)==S_OK);
    ShowWindow(hwnd,SW_HIDE);
    assert(IDXGISwapChain_Present(chain,0,0)==S_OK);
    DXGI_SWAP_CHAIN_DESC1 d={0};d.Width=160;d.Height=120;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *second=NULL;assert(IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown*)device,hwnd,&d,NULL,NULL,&second)==S_OK);
    ID3D11Device *other=NULL;ID3D11DeviceContext *oc=NULL;IDXGISwapChain *os=NULL;
    assert(D3D11CreateDeviceAndSwapChain(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&desc,&os,&other,NULL,&oc)==S_OK);
    assert(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,&invalid,1,D3D11_SDK_VERSION,NULL,NULL,NULL)==E_INVALIDARG);
    puts("render_diagnostic: real device/swapchain/Present PE calls passed");fflush(stdout);
    Sleep(8000);
    IDXGISwapChain_Release(os);ID3D11DeviceContext_Release(oc);ID3D11Device_Release(other);
    IDXGISwapChain1_Release(second);IDXGISwapChain1_Release(one);IDXGISwapChain_Release(chain);IDXGIFactory2_Release(factory);
    ID3D11DeviceContext_Release(context);ID3D11Device_Release(device);DestroyWindow(hwnd);return 0;
}
