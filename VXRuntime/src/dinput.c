// DirectInput8 root objects. Device operations remain explicit failures until a real input backend exists.
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "vx.h"

typedef int32_t HRESULT;
typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } GUID;
#define S_OK ((HRESULT)0)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER ((HRESULT)0x80004003)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_OUTOFMEMORY ((HRESULT)0x8007000e)
#define DIERR_NOTINITIALIZED ((HRESULT)0x80070015)
#define DIERR_OLDDIRECTINPUTVERSION ((HRESULT)0x8007047e)
#define DIERR_BETADIRECTINPUTVERSION ((HRESULT)0x80070481)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)
static const GUID iid_unknown = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const GUID iid_a = {0xbf798030,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0}};
static const GUID iid_w = {0xbf798031,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0}};
static int equal(const GUID *a, const GUID *b) { return !memcmp(a,b,sizeof *a); }

typedef struct di_root di_root;
typedef struct di_iface di_iface;
typedef BOOL (WINAPI *enum_callback)(const void *, void *);
typedef BOOL (WINAPI *semantics_callback)(const void *, void *, DWORD, DWORD, void *);
typedef BOOL (WINAPI *configure_callback)(void *, void *);
// A/W differ only in pointed-to string/record encodings; unsupported methods never interpret those records.
typedef struct {
    HRESULT (WINAPI *QueryInterface)(di_iface *, const GUID *, void **);
    uint32_t (WINAPI *AddRef)(di_iface *);
    uint32_t (WINAPI *Release)(di_iface *);
    HRESULT (WINAPI *CreateDevice)(di_iface *, const GUID *, void **, void *);
    HRESULT (WINAPI *EnumDevices)(di_iface *, DWORD, enum_callback, void *, DWORD);
    HRESULT (WINAPI *GetDeviceStatus)(di_iface *, const GUID *);
    HRESULT (WINAPI *RunControlPanel)(di_iface *, HANDLE, DWORD);
    HRESULT (WINAPI *Initialize)(di_iface *, HANDLE, DWORD);
    HRESULT (WINAPI *FindDevice)(di_iface *, const GUID *, const void *, GUID *);
    HRESULT (WINAPI *EnumDevicesBySemantics)(di_iface *, const void *, void *, semantics_callback, void *, DWORD);
    HRESULT (WINAPI *ConfigureDevices)(di_iface *, configure_callback, void *, DWORD, void *);
} di_vtable;
struct di_iface { const di_vtable *vtable; di_root *root; };
struct di_root {
    di_iface a, w;
    _Atomic uint32_t references;
    HANDLE instance;
    DWORD version;
};
_Static_assert(sizeof(GUID)==16 && sizeof(HRESULT)==4, "DirectInput scalar ABI");
_Static_assert(sizeof(di_vtable)==88 && offsetof(di_vtable,EnumDevices)==32, "DirectInput vtable ABI");

static uint32_t WINAPI di_addref(di_iface *self) {
    return atomic_fetch_add_explicit(&self->root->references,1,memory_order_relaxed)+1;
}
static uint32_t WINAPI di_release(di_iface *self) {
    di_root *root=self->root;
    uint32_t left=atomic_fetch_sub_explicit(&root->references,1,memory_order_acq_rel)-1;
    if(!left) free(root);
    return left;
}
static HRESULT WINAPI di_query(di_iface *self, const GUID *iid, void **out) {
    if(!out)return E_POINTER;
    *out=NULL;
    if(!iid)return E_POINTER;
    if(equal(iid,&iid_unknown)||equal(iid,&iid_a))*out=&self->root->a;
    else if(equal(iid,&iid_w))*out=&self->root->w;
    else return E_NOINTERFACE;
    di_addref(self);
    return S_OK;
}
static HRESULT validate_initialization(HANDLE instance, DWORD version) {
    const vx_image *image=instance?vx_image_at((uintptr_t)instance):NULL;
    if(!image||image->base!=(uint8_t*)instance||image->unloaded)return E_INVALIDARG;
    if(!version)return DIERR_NOTINITIALIZED;
    if(version<0x0800)return DIERR_BETADIRECTINPUTVERSION;
    if(version>0x0800)return DIERR_OLDDIRECTINPUTVERSION;
    return S_OK;
}
static HRESULT WINAPI di_initialize(di_iface *self, HANDLE instance, DWORD version) {
    (void)self;
    // Creation initializes before publishing either interface; repeat Initialize is validated and idempotent.
    return validate_initialization(instance,version);
}
static HRESULT WINAPI di_create_device(di_iface *self, const GUID *guid, void **out, void *outer) {
    (void)self;
    if(!out)return E_POINTER;
    *out=NULL;
    if(!guid)return E_POINTER;
    if(outer)return CLASS_E_NOAGGREGATION;
    return E_NOTIMPL;
}
static HRESULT WINAPI di_enum(di_iface *self, DWORD type, enum_callback callback, void *context, DWORD flags) {
    (void)self; (void)type; (void)context; (void)flags;
    if(!callback)return E_INVALIDARG;
    return E_NOTIMPL;                            // never turn an unimplemented inventory into an empty success
}
static HRESULT WINAPI di_status(di_iface *self, const GUID *guid) {
    (void)self;
    return guid?E_NOTIMPL:E_POINTER;
}
static HRESULT WINAPI di_panel(di_iface *self, HANDLE window, DWORD flags) {
    (void)self; (void)window; (void)flags;
    return E_NOTIMPL;
}
static HRESULT WINAPI di_find(di_iface *self, const GUID *guid, const void *name, GUID *out) {
    (void)self; (void)name;
    if(!out)return E_POINTER;
    memset(out,0,sizeof *out);
    return guid?E_NOTIMPL:E_POINTER;
}
static HRESULT WINAPI di_semantics(di_iface *self, const void *username, void *format, semantics_callback callback, void *context, DWORD flags) {
    (void)self; (void)username; (void)context; (void)flags;
    if(!format||!callback)return E_INVALIDARG;
    return E_NOTIMPL;
}
static HRESULT WINAPI di_configure(di_iface *self, configure_callback callback, void *params, DWORD flags, void *context) {
    (void)self; (void)callback; (void)flags; (void)context;
    return params?E_NOTIMPL:E_INVALIDARG;
}
static const di_vtable root_vtable = {
    di_query, di_addref, di_release, di_create_device, di_enum, di_status, di_panel,
    di_initialize, di_find, di_semantics, di_configure
};
static HRESULT WINAPI DirectInput8Create(HANDLE instance, DWORD version, const GUID *iid, void **out, void *outer) {
    if(!out)return E_POINTER;
    *out=NULL;
    if(!iid)return E_POINTER;
    if(outer)return CLASS_E_NOAGGREGATION;
    if(!equal(iid,&iid_a)&&!equal(iid,&iid_w))return E_NOINTERFACE;
    HRESULT hr=validate_initialization(instance,version);
    if(hr!=S_OK)return hr;
    di_root *root=calloc(1,sizeof *root);
    if(!root)return E_OUTOFMEMORY;
    root->a=(di_iface){&root_vtable,root};
    root->w=(di_iface){&root_vtable,root};
    atomic_init(&root->references,1);
    root->instance=instance;root->version=version;
    *out=equal(iid,&iid_a)?&root->a:&root->w;
    return S_OK;
}
void vx_dinput_init(void) {
    static const vx_export exports[]={VX_EXPORT(DirectInput8Create)};
    vx_register_module("dinput8.dll",exports,sizeof exports/sizeof exports[0]);
}
#ifdef __aarch64__
#include "thunks/dinput.c.inc"
#endif
