// Sparse raw/structured SRVs: real GPU reads, view bounds, CB sizes and VB slot collisions.
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shaders/high_srv_vs.h"
#include "shaders/high_srv_ps.h"
#include "shaders/high_srv_cs.h"
#include "shaders/high_srv_gs.h"
static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s FAILED\n", __LINE__, #c); failures++; } } while (0)
#define HR(c) do { HRESULT h_ = (c); if (FAILED(h_)) { printf("line %d: %s HRESULT %08lx\n", __LINE__, #c, h_); exit(1); } } while (0)
static ID3D11Buffer *buffer(UINT size, UINT bind, UINT misc, UINT stride, const void *data) {
    D3D11_BUFFER_DESC d = {size, D3D11_USAGE_DEFAULT, bind, 0, misc, stride};
    D3D11_SUBRESOURCE_DATA init = {data, 0, 0}; ID3D11Buffer *b;
    HR(ID3D11Device_CreateBuffer(dev, &d, data ? &init : NULL, &b)); return b;
}
static ID3D11ShaderResourceView *view(ID3D11Buffer *b, int raw, UINT first, UINT count) {
    D3D11_SHADER_RESOURCE_VIEW_DESC d = {0}; ID3D11ShaderResourceView *s;
    d.Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = raw ? D3D11_SRV_DIMENSION_BUFFEREX : D3D11_SRV_DIMENSION_BUFFER;
    d.BufferEx.FirstElement = first; d.BufferEx.NumElements = count;
    d.BufferEx.Flags = raw ? D3D11_BUFFEREX_SRV_FLAG_RAW : 0;
    HR(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)b, &d, &s)); return s;
}
static void read_buffer(ID3D11Buffer *b, UINT *out, UINT size) {
    D3D11_BUFFER_DESC d = {size, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ID3D11Buffer *s; D3D11_MAPPED_SUBRESOURCE m;
    HR(ID3D11Device_CreateBuffer(dev, &d, NULL, &s));
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)s, (ID3D11Resource *)b);
    HR(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)s, 0, D3D11_MAP_READ, 0, &m));
    memcpy(out, m.pData, size); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s, 0); ID3D11Buffer_Release(s);
}
#define BIND(stage, slot, srv) ID3D11DeviceContext_##stage##SetShaderResources(ctx, slot, 1, &(srv))
#define GET_CHECK(stage, slot, expected) do { ID3D11ShaderResourceView *got = NULL; ID3D11DeviceContext_##stage##GetShaderResources(ctx, slot, 1, &got); CHECK(got == (expected)); if (got) ID3D11ShaderResourceView_Release(got); } while (0)
// Each stage must retain high slots, replace them and release them when unbound.
static void lifetime(void) {
    UINT words[4] = {1,2,3,4};
    ID3D11Buffer *b = buffer(sizeof words,D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,4,words);
    ID3D11ShaderResourceView *s = view(b,0,0,2), *replacement = view(b,0,2,2), *none = NULL;
    BIND(VS,127,s); BIND(PS,127,s); BIND(GS,127,s); BIND(CS,127,s);
    CHECK(ID3D11ShaderResourceView_Release(s) == 4);
    GET_CHECK(VS,127,s); GET_CHECK(PS,127,s); GET_CHECK(GS,127,s); GET_CHECK(CS,127,s);
    // Hold one reference while replacing the last stage so the final count is observable.
    ID3D11ShaderResourceView_AddRef(s);
    BIND(VS,127,replacement); BIND(PS,127,replacement); BIND(GS,127,replacement); BIND(CS,127,replacement);
    CHECK(ID3D11ShaderResourceView_Release(s) == 0);
    GET_CHECK(VS,127,replacement); GET_CHECK(PS,127,replacement); GET_CHECK(GS,127,replacement); GET_CHECK(CS,127,replacement);
    BIND(VS,127,none); BIND(PS,127,none); BIND(GS,127,none); BIND(CS,127,none);
    GET_CHECK(VS,127,NULL); GET_CHECK(PS,127,NULL); GET_CHECK(GS,127,NULL); GET_CHECK(CS,127,NULL);
    CHECK(ID3D11ShaderResourceView_Release(replacement) == 0);
    ID3D11Buffer_Release(b);
    printf("  t127 VS/PS/GS/CS retain, Get, replace and unbind checked\n");
}
static void compute(void) {
    UINT words[] = {999, 11, 22, 777, 888}, idx[] = {1, 2, 0, 0}, constants[] = {41,0,0,0,73,0,0,0};
    ID3D11Buffer *b = buffer(sizeof words, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, words);
    ID3D11Buffer *raw = buffer(sizeof words, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, words);
    ID3D11ShaderResourceView *s = view(b, 0, 1, 2), *r = view(raw, 1, 2, 2), *none = NULL;
    ID3D11Buffer *cb = buffer(sizeof constants, D3D11_BIND_CONSTANT_BUFFER, 0, 0, constants);
    ID3D11Buffer *ib = buffer(sizeof idx, D3D11_BIND_CONSTANT_BUFFER, 0, 0, idx);
    ID3D11Buffer *out = buffer(64, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, NULL);
    ID3D11UnorderedAccessView *u; HR(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)out, NULL, &u));
    ID3D11ComputeShader *cs; HR(ID3D11Device_CreateComputeShader(dev, high_srv_cs, sizeof high_srv_cs, NULL, &cs));
    ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
    BIND(CS,33,s); BIND(CS,64,r); BIND(CS,127,s);
    GET_CHECK(CS,33,s); GET_CHECK(CS,64,r); GET_CHECK(CS,127,s);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 1, &ib); ID3D11DeviceContext_CSSetConstantBuffers(ctx, 9, 1, &cb);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &u, NULL);
    ID3D11DeviceContext_Dispatch(ctx, 1, 1, 1);
    UINT got[16] = {0}, expected[] = {11,22,0,22,777,0,2,4,8,73,0,22,0};
    read_buffer(out, got, sizeof got);
    for (UINT i = 0; i < sizeof expected / sizeof *expected; i++) { printf("  CS[%u] = %u (expected %u)\n", i, got[i], expected[i]); CHECK(got[i] == expected[i]); }
    BIND(CS,33,none); BIND(CS,64,none); BIND(CS,127,none);
    GET_CHECK(CS,33,NULL); GET_CHECK(CS,64,NULL); GET_CHECK(CS,127,NULL);
    ID3D11UnorderedAccessView *nu = NULL; ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &nu, NULL);
    ID3D11ShaderResourceView_Release(s); ID3D11ShaderResourceView_Release(r);
    ID3D11UnorderedAccessView_Release(u); ID3D11ComputeShader_Release(cs);
    ID3D11Buffer_Release(b); ID3D11Buffer_Release(raw); ID3D11Buffer_Release(cb); ID3D11Buffer_Release(ib); ID3D11Buffer_Release(out);
}
static void graphics(void) {
    float values[][4] = {{9,9,9,9}, {0.125f,0,0,0}, {0,0.25f,0,0}, {7,7,7,7}};
    float a[] = {0,0,0.125f,0}, b[] = {0,0,0.125f,0};
    float vertices[][2] = {{-1,-1},{-1,3},{3,-1}};
    UINT words[] = {999,32,16,888};
    ID3D11Buffer *sb = buffer(sizeof values,D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,16,values);
    ID3D11Buffer *rb = buffer(sizeof words,D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,0,words);
    ID3D11Buffer *vb = buffer(sizeof vertices,D3D11_BIND_VERTEX_BUFFER,0,0,vertices);
    ID3D11Buffer *cbs[] = {buffer(16,D3D11_BIND_CONSTANT_BUFFER,0,0,a),buffer(16,D3D11_BIND_CONSTANT_BUFFER,0,0,b)};
    ID3D11ShaderResourceView *s = view(sb,0,1,2), *r = view(rb,1,1,2), *none = NULL;
    ID3D11VertexShader *vs; ID3D11PixelShader *ps; ID3D11GeometryShader *gs;
    HR(ID3D11Device_CreateVertexShader(dev,high_srv_vs,sizeof high_srv_vs,NULL,&vs));
    HR(ID3D11Device_CreatePixelShader(dev,high_srv_ps,sizeof high_srv_ps,NULL,&ps));
    HR(ID3D11Device_CreateGeometryShader(dev,high_srv_gs,sizeof high_srv_gs,NULL,&gs));
    D3D11_INPUT_ELEMENT_DESC el = {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,8,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    ID3D11InputLayout *layout; HR(ID3D11Device_CreateInputLayout(dev,&el,1,high_srv_vs,sizeof high_srv_vs,&layout));
    UINT stride=8, offset=0;
    ID3D11DeviceContext_IASetInputLayout(ctx,layout); ID3D11DeviceContext_IASetVertexBuffers(ctx,8,1,&vb,&stride,&offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0); ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx,0,2,cbs);
    BIND(VS,33,s); BIND(VS,38,s); BIND(PS,127,r); BIND(GS,127,s);
    GET_CHECK(VS,33,s); GET_CHECK(VS,38,s); GET_CHECK(PS,127,r); GET_CHECK(GS,127,s);
    D3D11_TEXTURE2D_DESC td={4,4,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
    ID3D11Texture2D *target,*staging; ID3D11RenderTargetView *rt;
    HR(ID3D11Device_CreateTexture2D(dev,&td,NULL,&target)); HR(ID3D11Device_CreateRenderTargetView(dev,(ID3D11Resource *)target,NULL,&rt));
    td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    HR(ID3D11Device_CreateTexture2D(dev,&td,NULL,&staging));
    D3D11_VIEWPORT vp={0,0,4,4,0,1}; ID3D11DeviceContext_RSSetViewports(ctx,1,&vp);
    D3D11_RASTERIZER_DESC rd={0}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
    ID3D11RasterizerState *rs; HR(ID3D11Device_CreateRasterizerState(dev,&rd,&rs)); ID3D11DeviceContext_RSSetState(ctx,rs);
    D3D11_DEPTH_STENCIL_DESC dd = {0}; ID3D11DepthStencilState *ds;
    HR(ID3D11Device_CreateDepthStencilState(dev,&dd,&ds)); ID3D11DeviceContext_OMSetDepthStencilState(ctx,ds,0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&rt,NULL);
    for (int geometry=0; geometry<2; geometry++) {
        float clear[4]={0}; ID3D11DeviceContext_ClearRenderTargetView(ctx,rt,clear);
        ID3D11DeviceContext_GSSetShader(ctx,geometry ? gs : NULL,NULL,0); ID3D11DeviceContext_Draw(ctx,3,0);
        ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource *)staging,(ID3D11Resource *)target);
        D3D11_MAPPED_SUBRESOURCE m; HR(ID3D11DeviceContext_Map(ctx,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&m));
        UINT px=*(UINT *)((BYTE *)m.pData+2*m.RowPitch+8); ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource *)staging,0);
        printf("  VS/PS%s VB8 pixel = %08x\n",geometry ? "/GS" : "",px);
        CHECK((px&255)==(geometry ? 96 : 64)); CHECK(((px>>8)&255)==80); CHECK(((px>>16)&255)==64); CHECK((px>>24)==255);
    }
    BIND(VS,33,none); BIND(VS,38,none); BIND(PS,127,none); BIND(GS,127,none);
    GET_CHECK(VS,33,NULL); GET_CHECK(VS,38,NULL); GET_CHECK(PS,127,NULL); GET_CHECK(GS,127,NULL);
    ID3D11ShaderResourceView_Release(s); ID3D11ShaderResourceView_Release(r);
    ID3D11Buffer_Release(sb); ID3D11Buffer_Release(rb); ID3D11Buffer_Release(vb); ID3D11Buffer_Release(cbs[0]); ID3D11Buffer_Release(cbs[1]);
    ID3D11Texture2D_Release(target); ID3D11Texture2D_Release(staging); ID3D11RenderTargetView_Release(rt);
    ID3D11VertexShader_Release(vs); ID3D11PixelShader_Release(ps); ID3D11GeometryShader_Release(gs); ID3D11InputLayout_Release(layout); ID3D11RasterizerState_Release(rs); ID3D11DepthStencilState_Release(ds);
}
int main(void) {
    D3D_FEATURE_LEVEL level;
    HR(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&dev,&level,&ctx));
    lifetime(); compute(); graphics();
    ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(dev);
    printf("high_srv: %d failed checks -> %s\n",failures,failures ? "FAILED" : "ok"); return failures ? 1 : 0;
}
