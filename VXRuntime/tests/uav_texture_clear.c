// UAV clears preserve bit patterns, view ranges and ordering with queued clears/copies.
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>
static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static void test_2d(DXGI_FORMAT resource, DXGI_FORMAT view, UINT bpp, int use_float, const void *values, const void *expected, int rtv) {
    D3D11_TEXTURE2D_DESC td = { 8, 4, 3, 3, resource, {1, 0}, D3D11_USAGE_DEFAULT,
        D3D11_BIND_UNORDERED_ACCESS | (rtv ? D3D11_BIND_RENDER_TARGET : 0), 0, 0 };
    BYTE initial[8 * 4 * 16]; memset(initial, 0x5a, sizeof initial);
    D3D11_SUBRESOURCE_DATA init[9];
    for (UINT i = 0; i < 9; i++) { init[i].pSysMem = initial; init[i].SysMemPitch = (8 >> (i % 3)) * bpp; init[i].SysMemSlicePitch = 0; }
    ID3D11Texture2D *texture = NULL, *staging = NULL, *before = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, init, &texture)));
    if (!texture) return;
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging)));
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &before)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = { view, D3D11_UAV_DIMENSION_TEXTURE2DARRAY };
    ud.Texture2DArray.MipSlice = 1; ud.Texture2DArray.FirstArraySlice = 1; ud.Texture2DArray.ArraySize = 2;
    ID3D11UnorderedAccessView *uav = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)texture, &ud, &uav)));
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)before, (ID3D11Resource *)texture);
    if (rtv) {
        D3D11_RENDER_TARGET_VIEW_DESC rd = { view, D3D11_RTV_DIMENSION_TEXTURE2DARRAY };
        rd.Texture2DArray.MipSlice = 1; rd.Texture2DArray.FirstArraySlice = 1; rd.Texture2DArray.ArraySize = 1;
        ID3D11RenderTargetView *target = NULL;
        CHECK(SUCCEEDED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)texture, &rd, &target)));
        const float color[4] = { 0.125f, 0.25f, 0.5f, 1 };
        ID3D11DeviceContext_ClearRenderTargetView(ctx, target, color);
        ID3D11RenderTargetView_Release(target);
    }
    if (use_float) ID3D11DeviceContext_ClearUnorderedAccessViewFloat(ctx, uav, values);
    else ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uav, values);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    // A later clear must not change either previously queued copy.
    const UINT later[4] = { 0, 0, 0, 0 };
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uav, later);
    for (UINT pass = 0; pass < 2; pass++) for (UINT sub = 0; sub < 9; sub++) {
        ID3D11Resource *read = (ID3D11Resource *)(pass ? staging : before);
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ID3D11DeviceContext_Map(ctx, read, sub, D3D11_MAP_READ, 0, &m);
        CHECK(SUCCEEDED(hr));
        if (FAILED(hr)) continue;
        int changed = pass && sub % 3 == 1 && sub / 3 >= 1, ok = 1;
        UINT width = 8 >> (sub % 3), height = 4 >> (sub % 3);
        for (UINT y = 0; y < height; y++) for (UINT x = 0; x < width; x++)
            if (memcmp((BYTE *)m.pData + y * m.RowPitch + x * bpp, changed ? expected : initial, bpp)) ok = 0;
        CHECK(ok);
        ID3D11DeviceContext_Unmap(ctx, read, sub);
    }
    printf("2D resource=%u view=%u float=%d rtv=%d failures=%d\n", resource, view, use_float, rtv, failures);
    ID3D11UnorderedAccessView_Release(uav); ID3D11Texture2D_Release(texture);
    ID3D11Texture2D_Release(staging); ID3D11Texture2D_Release(before);
}
static void test_1d(void) {
    D3D11_TEXTURE1D_DESC td = { 8, 1, 3, DXGI_FORMAT_R32_UINT, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    UINT initial[8] = { 7,7,7,7,7,7,7,7 };
    D3D11_SUBRESOURCE_DATA init[3];
    for (UINT i = 0; i < 3; i++) { init[i].pSysMem = initial; init[i].SysMemPitch = 32; init[i].SysMemSlicePitch = 0; }
    ID3D11Texture1D *texture = NULL, *staging = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture1D(dev, &td, init, &texture)));
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture1D(dev, &td, NULL, &staging)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = { DXGI_FORMAT_R32_UINT, D3D11_UAV_DIMENSION_TEXTURE1DARRAY };
    ud.Texture1DArray.MipSlice = 0; ud.Texture1DArray.FirstArraySlice = 1; ud.Texture1DArray.ArraySize = 2;
    ID3D11UnorderedAccessView *uav = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)texture, &ud, &uav)));
    const UINT values[4] = { 0xabcdef01, 2, 3, 4 };
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uav, values);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    for (UINT sub = 0; sub < 3; sub++) {
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, sub, D3D11_MAP_READ, 0, &m);
        CHECK(SUCCEEDED(hr)); if (FAILED(hr)) continue;
        for (UINT x = 0; x < 8; x++) CHECK(((UINT *)m.pData)[x] == (sub >= 1 ? values[0] : 7));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, sub);
    }
    ID3D11UnorderedAccessView_Release(uav); ID3D11Texture1D_Release(texture); ID3D11Texture1D_Release(staging);
}
static void test_3d(UINT count) {
    D3D11_TEXTURE3D_DESC td = { 8, 4, 8, 2, DXGI_FORMAT_R32_UINT, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    UINT initial[256]; for (UINT i = 0; i < 256; i++) initial[i] = 0x12345678;
    D3D11_SUBRESOURCE_DATA init[2] = {{ initial, 32, 128 }, { initial, 16, 32 }};
    ID3D11Texture3D *texture = NULL, *staging = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture3D(dev, &td, init, &texture)));
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture3D(dev, &td, NULL, &staging)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = { DXGI_FORMAT_R32_UINT, D3D11_UAV_DIMENSION_TEXTURE3D };
    ud.Texture3D.MipSlice = 1; ud.Texture3D.FirstWSlice = 1; ud.Texture3D.WSize = count;
    ID3D11UnorderedAccessView *uav = NULL;
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)texture, &ud, &uav)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC got; ID3D11UnorderedAccessView_GetDesc(uav, &got);
    CHECK(got.Texture3D.FirstWSlice == 1 && got.Texture3D.WSize == count);
    UINT values[4] = { 0xdeadbeef, 2, 3, 4 };
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uav, values);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    for (UINT sub = 0; sub < 2; sub++) {
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, sub, D3D11_MAP_READ, 0, &m);
        CHECK(SUCCEEDED(hr)); if (FAILED(hr)) continue;
        int ok = 1;
        for (UINT z = 0; z < (8u >> sub); z++) for (UINT y = 0; y < (4u >> sub); y++) for (UINT x = 0; x < (8u >> sub); x++) {
            UINT value = *(UINT *)((BYTE *)m.pData + z * m.DepthPitch + y * m.RowPitch + x * 4);
            if (value != (sub == 1 && z >= 1 && (count == ~0u || z < 3) ? values[0] : initial[0])) ok = 0;
        }
        CHECK(ok); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, sub);
    }
    ID3D11UnorderedAccessView_Release(uav); ID3D11Texture3D_Release(texture); ID3D11Texture3D_Release(staging);
}
// Queue every clear and snapshot before mapping: eviction must not recycle an in-flight pattern.
// The larger case exceeds the tile size, exercises its short tail and exhausts the byte budget.
static void test_cache(UINT width, UINT height, UINT rounds) {
    D3D11_TEXTURE2D_DESC td = { width, height, 1, 1, DXGI_FORMAT_R32G32B32A32_UINT, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    ID3D11Texture2D *texture = NULL, *snapshots[20] = {0};
    ID3D11UnorderedAccessView *uav = NULL;
    UINT expected[20][4];
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &texture)));
    if (!texture) return;
    CHECK(SUCCEEDED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)texture, NULL, &uav)));
    if (!uav) { ID3D11Texture2D_Release(texture); return; }
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (UINT i = 0; i < rounds; i++) {
        UINT pattern = i == 2 || i == 3 || i == rounds - 1 ? 0 : i;
        expected[i][0] = 0x7fc12345u + pattern;
        expected[i][1] = 0xffffffffu - pattern;
        expected[i][2] = 0x80001234u + pattern;
        expected[i][3] = 0x3f800001u + pattern;
        CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &snapshots[i])));
        ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uav, expected[i]);
        if (snapshots[i]) ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)snapshots[i], (ID3D11Resource *)texture);
    }
    for (UINT i = 0; i < rounds; i++) {
        if (!snapshots[i]) continue;
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)snapshots[i], 0, D3D11_MAP_READ, 0, &m);
        CHECK(SUCCEEDED(hr));
        if (SUCCEEDED(hr)) {
            int ok = 1;
            for (UINT y = 0; y < height; y++) for (UINT x = 0; x < width; x++)
                if (memcmp((BYTE *)m.pData + y * m.RowPitch + x * 16, expected[i], 16)) ok = 0;
            CHECK(ok);
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)snapshots[i], 0);
        }
        ID3D11Texture2D_Release(snapshots[i]);
    }
    ID3D11UnorderedAccessView_Release(uav); ID3D11Texture2D_Release(texture);
    printf("cache %ux%u rounds=%u failures=%d\n", width, height, rounds, failures);
}
int main(void) {
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx))) return 1;
    const UINT u[4] = { 0x3f800001, 0x80001234, 0x7fc12345, 0xffffffff };
    const float f[4] = { 0.25f, -0.5f, 2.0f, 1.0f };
    const BYTE low[4] = { 1, 0x34, 0x45, 0xff }, norm[4] = { 64, 0, 255, 255 };
    const signed char snorm[4] = { 32, -64, 127, 127 };
    const USHORT half[4] = { 0x3400, 0xb800, 0x4000, 0x3c00 };
    test_2d(DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_UINT, 16, 0, u, u, 0);
    test_2d(DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 0, u, u, 0);
    test_2d(DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, f, f, 1);
    test_2d(DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, 4, 1, f, f, 1);
    test_2d(DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_UINT, 4, 0, u, u, 0);
    test_2d(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 0, u, low, 0);
    test_2d(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 1, f, norm, 0);
    test_2d(DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SNORM, 4, 1, f, snorm, 0);
    test_2d(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, 1, f, half, 0);
    const UINT packed_uint = (u[0] & 1023) | ((u[1] & 1023) << 10) | ((u[2] & 1023) << 20) | ((u[3] & 3) << 30);
    const UINT packed_norm = 256 | (1023u << 20) | (3u << 30);
    const UINT packed_float = 0x340 | (0u << 11) | (0x200u << 22);
    test_2d(DXGI_FORMAT_R10G10B10A2_UINT, DXGI_FORMAT_R10G10B10A2_UINT, 4, 0, u, &packed_uint, 0);
    test_2d(DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, 4, 1, f, &packed_norm, 0);
    test_2d(DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, 4, 1, f, &packed_float, 0);
    test_1d(); test_3d(2); test_3d(~0u);
    test_cache(17, 5, 20); test_cache(1024, 513, 10);
    ID3D11DeviceContext_ClearState(ctx); ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(dev);
    // A fresh context/device must build its own planes after the previous cache has been destroyed.
    dev = NULL; ctx = NULL;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx);
    CHECK(SUCCEEDED(hr));
    if (SUCCEEDED(hr)) {
        test_cache(17, 5, 4);
        ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(dev);
    }
    printf("uav_texture_clear: %d failures\n", failures); return failures ? 1 : 0;
}
