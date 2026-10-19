// R32 typeless UAV backing must allow integer atomics and preserve floating-point views.
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "shaders/atomic_cs.h"
#include "shaders/append_cs.h"
static int failures;
#define CHECK(c) do { if (!(c)) { printf("check failed (line %d): %s\n", __LINE__, #c); failures++; } } while (0)
static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;

static ID3D11Buffer *make_buffer(UINT size, UINT bind, UINT misc, UINT stride, const void *data, UINT usage, UINT cpu) {
    D3D11_BUFFER_DESC d = { size, usage, bind, cpu, misc, stride };
    D3D11_SUBRESOURCE_DATA init = { data, 0, 0 };
    ID3D11Buffer *b = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateBuffer(dev, &d, data ? &init : NULL, &b)));
    return b;
}
static void read_buffer(ID3D11Buffer *src, void *out, UINT size) {
    ID3D11Buffer *staging = make_buffer(size, 0, 0, 0, NULL, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)src);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
        memcpy(out, m.pData, size);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    }
    ID3D11Buffer_Release(staging);
}

static void test_float(DXGI_FORMAT format) {
    ID3D11ComputeShader *cs;
    CHECK(SUCCEEDED(ID3D11Device_CreateComputeShader(dev, atomic_cs, sizeof atomic_cs, NULL, &cs)));
    UINT zero[4] = { 7, 7, 7, 7 };
    ID3D11Buffer *counters = make_buffer(16, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, zero, D3D11_USAGE_DEFAULT, 0);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = { DXGI_FORMAT_R32_TYPELESS, D3D11_UAV_DIMENSION_BUFFER };
    ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ID3D11UnorderedAccessView *uavs[2];
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)counters, &ud, &uavs[0])));
    UINT clear[4] = { 0, 0, 0, 0 };
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uavs[0], clear);   // the initial 7s go away
    D3D11_TEXTURE2D_DESC td = { 16, 16, 1, 1, format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    ID3D11Texture2D *image;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &image)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC image_view = { DXGI_FORMAT_R32_FLOAT, D3D11_UAV_DIMENSION_TEXTURE2D };
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)image, &image_view, &uavs[1])));
    ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 2, uavs, NULL);
    ID3D11DeviceContext_Dispatch(ctx, 2, 2, 1);
    UINT result[4];
    read_buffer(counters, result, sizeof result);
    CHECK(result[0] == 256 && result[1] == 1515 && result[2] == 0xffff && result[3] == 0);
    printf("  atomics: count %u, max %u, or %#x, untouched %u\n", result[0], result[1], result[2], result[3]);
    // The image: x and y as red and green.
    td.MiscFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging;
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)image);
    D3D11_MAPPED_SUBRESOURCE m;
    float p0 = 0, p1 = 0;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
        p0 = *(float *)((BYTE *)m.pData + 0 * m.RowPitch + 15 * 4);   // (15, 0): red full
        p1 = *(float *)((BYTE *)m.pData + 5 * m.RowPitch + 3 * 4);    // (3, 5)
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    }
    CHECK(p0 == 1.0f && fabsf(p1 - 0.2f) < 0.00001f);
    printf("float UAV format %u: %g %g\n", format, p0, p1);
    ID3D11Texture2D_Release(staging); ID3D11UnorderedAccessView_Release(uavs[0]); ID3D11UnorderedAccessView_Release(uavs[1]);
    ID3D11Texture2D_Release(image); ID3D11Buffer_Release(counters); ID3D11ComputeShader_Release(cs);
}
static void test_integer(DXGI_FORMAT format, UINT misc) {
    ID3D11ComputeShader *cs;
    CHECK(SUCCEEDED(ID3D11Device_CreateComputeShader(dev, append_cs, sizeof append_cs, NULL, &cs)));
    UINT values[128];
    for (int i = 0; i < 128; i++) values[i] = (UINT)i;
    ID3D11Buffer *in = make_buffer(sizeof values, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, values, D3D11_USAGE_DEFAULT, 0);
    ID3D11Buffer *out = make_buffer(sizeof values, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, NULL, D3D11_USAGE_DEFAULT, 0);
    ID3D11Buffer *count = make_buffer(16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, NULL, D3D11_USAGE_DEFAULT, 0);
    ID3D11Buffer *count_copy = make_buffer(16, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, NULL, D3D11_USAGE_DEFAULT, 0);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = { DXGI_FORMAT_UNKNOWN, D3D11_UAV_DIMENSION_BUFFER };
    ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = 128; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    ID3D11UnorderedAccessView *uavs[3];
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)in, &ud, &uavs[0])));
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)out, &ud, &uavs[1])));
    D3D11_TEXTURE2D_DESC td = { 4, 1, 1, 1, format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, misc };
    UINT zeros[4] = { 0, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA init = { zeros, 16, 16 };
    ID3D11Texture2D *hist;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, &init, &hist)));
    if (misc) {
        IDXGIResource *resource = NULL;
        HANDLE handle = NULL;
        ID3D11Texture2D *opened = NULL;
        CHECK(SUCCEEDED(ID3D11Texture2D_QueryInterface(hist, &IID_IDXGIResource, (void **)&resource)));
        CHECK(resource && SUCCEEDED(IDXGIResource_GetSharedHandle(resource, &handle)));
        CHECK(SUCCEEDED(ID3D11Device_OpenSharedResource(dev, handle, &IID_ID3D11Texture2D, (void **)&opened)));
        if (opened) { ID3D11Texture2D_Release(hist); hist = opened; }
        if (resource) IDXGIResource_Release(resource);
    }
    D3D11_UNORDERED_ACCESS_VIEW_DESC hist_view = { DXGI_FORMAT_R32_UINT, D3D11_UAV_DIMENSION_TEXTURE2D };
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)hist, &hist_view, &uavs[2])));
    UINT initial[3] = { 128, 0, 0 };
    ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 3, uavs, initial);
    ID3D11DeviceContext_Dispatch(ctx, 2, 1, 1);
    ID3D11DeviceContext_CopyStructureCount(ctx, count_copy, 4, uavs[1]);
    ID3D11DeviceContext_CopyStructureCount(ctx, count_copy, 8, uavs[0]);
    UINT got[128], counts[4];
    read_buffer(out, got, sizeof got);
    read_buffer(count_copy, counts, sizeof counts);
    CHECK(counts[1] == 64 && counts[2] == 0);
    UINT seen = 0, sum = 0;
    for (int i = 0; i < 64; i++) { if (got[i] % 20 == 10 && got[i] < 1280) seen++; sum += got[i]; }
    CHECK(seen == 64 && sum == 10 * 64 * 64);
    td.MiscFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging;
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)hist);
    D3D11_MAPPED_SUBRESOURCE m;
    UINT bins[4] = { 0 };
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
        memcpy(bins, m.pData, sizeof bins);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    }
    CHECK(bins[0] == 32 && bins[1] == 32 && bins[2] == 32 && bins[3] == 32);
    printf("  append: %u appended (sum %u), %u left to consume, histogram %u %u %u %u\n", counts[1], sum, counts[2], bins[0], bins[1], bins[2], bins[3]);
    ID3D11UnorderedAccessView *none[3] = { NULL, NULL, NULL };
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 3, none, NULL);
    for (int i = 0; i < 3; i++) ID3D11UnorderedAccessView_Release(uavs[i]);
    ID3D11Texture2D_Release(staging); ID3D11Texture2D_Release(hist);
    ID3D11Buffer_Release(count_copy); ID3D11Buffer_Release(count); ID3D11Buffer_Release(out); ID3D11Buffer_Release(in);
    ID3D11ComputeShader_Release(cs);
}

static void test_float_rtv(void) {
    D3D11_TEXTURE2D_DESC td = { 4, 1, 1, 1, DXGI_FORMAT_R32_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT,
        D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture2D *texture = NULL, *staging = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    D3D11_RENDER_TARGET_VIEW_DESC rd = { DXGI_FORMAT_R32_FLOAT, D3D11_RTV_DIMENSION_TEXTURE2D };
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &texture)));
    CHECK(SUCCEEDED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)texture, &rd, &rtv)));
    float color[4] = { 0.25f, 0, 0, 0 };
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, color);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging)));
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    D3D11_MAPPED_SUBRESOURCE m;
    HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m);
    CHECK(SUCCEEDED(hr));
    if (SUCCEEDED(hr)) {
        for (int i = 0; i < 4; i++) CHECK(((float *)m.pData)[i] == 0.25f);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    }
    ID3D11RenderTargetView_Release(rtv); ID3D11Texture2D_Release(staging); ID3D11Texture2D_Release(texture);
}
int main(void) {
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx);
    if (FAILED(hr)) return 1;
    test_float(DXGI_FORMAT_R32_FLOAT);
    test_float(DXGI_FORMAT_R32_TYPELESS);
    test_integer(DXGI_FORMAT_R32_UINT, 0);
    test_integer(DXGI_FORMAT_R32_TYPELESS, 0);
    test_integer(DXGI_FORMAT_R32_TYPELESS, D3D11_RESOURCE_MISC_SHARED);
    test_integer(DXGI_FORMAT_R32_UINT, D3D11_RESOURCE_MISC_SHARED);
    test_float_rtv();
    ID3D11DeviceContext_ClearState(ctx);
    ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(dev);
    printf("atomic_texture: %d failures\n", failures);
    return failures ? 1 : 0;
}
