// Real root identity/lifetime; unsupported device methods must never look like an empty successful inventory.
#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <windows.h>
#include <dinput.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
static unsigned queries, adds, releases, callbacks;
static HRESULT WINAPI query(IUnknown *self, REFIID iid, void **out) {
    (void)self; (void)iid; queries++; if(out)*out=NULL; return E_NOINTERFACE;
}
static ULONG WINAPI addref(IUnknown *self) { (void)self; adds++; return 2; }
static ULONG WINAPI release(IUnknown *self) { (void)self; releases++; return 1; }
static IUnknownVtbl outer_vtable={query,addref,release};
static BOOL CALLBACK enumerate_a(const DIDEVICEINSTANCEA *device, void *context) { (void)device; (void)context; callbacks++; return DIENUM_CONTINUE; }
static BOOL CALLBACK enumerate_w(const DIDEVICEINSTANCEW *device, void *context) { (void)device; (void)context; callbacks++; return DIENUM_CONTINUE; }
static BOOL CALLBACK semantics_a(const DIDEVICEINSTANCEA *d, IDirectInputDevice8A *p, DWORD a, DWORD b, void *c) { (void)d;(void)p;(void)a;(void)b;(void)c;callbacks++;return TRUE; }
static BOOL CALLBACK semantics_w(const DIDEVICEINSTANCEW *d, IDirectInputDevice8W *p, DWORD a, DWORD b, void *c) { (void)d;(void)p;(void)a;(void)b;(void)c;callbacks++;return TRUE; }
static BOOL CALLBACK configure(IUnknown *u, void *c) { (void)u;(void)c;callbacks++;return TRUE; }
static const IID unknown={0x12345678,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
_Static_assert(sizeof(void*)==8&&sizeof(HRESULT)==4&&sizeof(DWORD)==4,"Windows x64 ABI");
_Static_assert(sizeof(IDirectInput8AVtbl)==88&&sizeof(IDirectInput8WVtbl)==88,"complete root vtables");
_Static_assert(offsetof(IDirectInput8AVtbl,EnumDevices)==32,"EnumDevices slot");
typedef HRESULT (WINAPI *create_fn)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
static HRESULT version_error(DWORD v) { return !v?DIERR_NOTINITIALIZED:v<0x800?DIERR_BETADIRECTINPUTVERSION:DIERR_OLDDIRECTINPUTVERSION; }
static unsigned creation_matrix(create_fn create) {
    const IID *ids[]={&IID_IDirectInput8A,&IID_IDirectInput8W,&IID_IUnknown,&unknown,NULL};
    const DWORD versions[]={0,0x700,0x800,0x900,0xffffffffu};IUnknown outer={&outer_vtable};unsigned calls=0;
    for(unsigned i=0;i<5;i++)for(unsigned v=0;v<5;v++)for(unsigned agg=0;agg<2;agg++){
        struct {uint64_t before;void*out;uint64_t after;} slot={0xaabbccddeeff0011ULL,(void*)(uintptr_t)0x12345678,0x1122334455667788ULL};
        IID original;if(ids[i])original=*ids[i];SetLastError(0x1234abcd);
        HRESULT expected=!ids[i]?E_POINTER:agg?CLASS_E_NOAGGREGATION:i>=2?E_NOINTERFACE:versions[v]!=0x800?version_error(versions[v]):S_OK;
        HRESULT hr=create(GetModuleHandleA(NULL),versions[v],ids[i],&slot.out,agg?&outer:NULL);
        assert(hr==expected&&GetLastError()==0x1234abcd);
        assert(slot.before==0xaabbccddeeff0011ULL&&slot.after==0x1122334455667788ULL);
        if(FAILED(hr))assert(slot.out==NULL);else {assert(slot.out);assert(IUnknown_Release((IUnknown*)slot.out)==0);}
        assert(create(NULL,versions[v],ids[i],NULL,agg?&outer:NULL)==E_POINTER&&GetLastError()==0x1234abcd);
        if(ids[i])assert(!memcmp(ids[i],&original,sizeof original));calls+=2;
    }
    assert(!queries&&!adds&&!releases&&outer.lpVtbl==&outer_vtable);
    return calls;
}
static void identity(IDirectInput8A *a) {
    IDirectInput8A *again=NULL;IDirectInput8W *w=NULL;IUnknown *ua=NULL,*uw=NULL;
    assert(IDirectInput8_QueryInterface(a,&IID_IDirectInput8A,(void**)&again)==S_OK&&again==a);
    assert(IDirectInput8_QueryInterface(a,&IID_IDirectInput8W,(void**)&w)==S_OK&&w);
    assert(IDirectInput8_QueryInterface(a,&IID_IUnknown,(void**)&ua)==S_OK);
    assert(IDirectInput8_QueryInterface(w,&IID_IUnknown,(void**)&uw)==S_OK&&ua==uw);
    IDirectInput8A *back=NULL;assert(IDirectInput8_QueryInterface(w,&IID_IDirectInput8A,(void**)&back)==S_OK&&back==a);
    assert(IDirectInput8_Release(back)==5);assert(IUnknown_Release(uw)==4);assert(IUnknown_Release(ua)==3);
    assert(IDirectInput8_Release(w)==2);assert(IDirectInput8_Release(again)==1);
    struct{uint64_t left;void*out;uint64_t right;} slot={7,(void*)1,9};
    assert(IDirectInput8_QueryInterface(a,&unknown,&slot.out)==E_NOINTERFACE&&slot.out==NULL&&slot.left==7&&slot.right==9);
    slot.out=(void*)1;assert(IDirectInput8_QueryInterface(a,NULL,&slot.out)==E_POINTER&&slot.out==NULL);
    assert(IDirectInput8_QueryInterface(a,&IID_IUnknown,NULL)==E_POINTER);
    assert(IDirectInput8_AddRef(a)==2);assert(IDirectInput8_Release(a)==1);
}
static void unsupported(IDirectInput8A *a) {
    IDirectInput8W *w=NULL;assert(IDirectInput8_QueryInterface(a,&IID_IDirectInput8W,(void**)&w)==S_OK);
    struct{uint64_t left;void*out;uint64_t right;} slot={7,(void*)1,9};
    SetLastError(0x1234abcd);
    assert(IDirectInput8_CreateDevice(a,&GUID_SysKeyboard,(IDirectInputDevice8A**)&slot.out,NULL)==E_NOTIMPL&&slot.out==NULL);
    slot.out=(void*)1;assert(IDirectInput8_CreateDevice(w,&GUID_SysMouse,(IDirectInputDevice8W**)&slot.out,NULL)==E_NOTIMPL&&slot.out==NULL);
    assert(slot.left==7&&slot.right==9);
    assert(IDirectInput8_CreateDevice(a,&GUID_SysKeyboard,NULL,NULL)==E_POINTER);
    slot.out=(void*)1;assert(IDirectInput8_CreateDevice(a,NULL,(IDirectInputDevice8A**)&slot.out,NULL)==E_POINTER&&slot.out==NULL);
    IUnknown outer={&outer_vtable};slot.out=(void*)1;
    assert(IDirectInput8_CreateDevice(a,&GUID_SysKeyboard,(IDirectInputDevice8A**)&slot.out,&outer)==CLASS_E_NOAGGREGATION&&slot.out==NULL);
    assert(IDirectInput8_EnumDevices(a,DI8DEVCLASS_ALL,enumerate_a,&slot,0)==E_NOTIMPL);
    assert(IDirectInput8_EnumDevices(w,DI8DEVCLASS_ALL,enumerate_w,&slot,0)==E_NOTIMPL);
    assert(IDirectInput8_EnumDevices(a,DI8DEVCLASS_ALL,NULL,&slot,0)==E_INVALIDARG);
    assert(IDirectInput8_GetDeviceStatus(a,&GUID_SysKeyboard)==E_NOTIMPL);
    assert(IDirectInput8_GetDeviceStatus(w,&GUID_SysMouse)==E_NOTIMPL);
    assert(IDirectInput8_GetDeviceStatus(a,NULL)==E_POINTER);
    assert(IDirectInput8_RunControlPanel(a,NULL,0)==E_NOTIMPL&&IDirectInput8_RunControlPanel(w,NULL,0)==E_NOTIMPL);
    struct{uint64_t left;GUID out;uint64_t right;} found;memset(&found,0xa7,sizeof found);GUID zero={0};
    assert(IDirectInput8_FindDevice(a,&GUID_SysKeyboard,"fixture",&found.out)==E_NOTIMPL&&!memcmp(&found.out,&zero,16));
    assert(found.left==0xa7a7a7a7a7a7a7a7ULL&&found.right==found.left);
    memset(&found.out,0xa7,16);assert(IDirectInput8_FindDevice(w,&GUID_SysMouse,L"fixture",&found.out)==E_NOTIMPL&&!memcmp(&found.out,&zero,16));
    assert(IDirectInput8_FindDevice(a,&GUID_SysKeyboard,NULL,NULL)==E_POINTER);
    DIACTIONFORMATA fa={0},fa_old;fa.dwSize=sizeof fa;fa_old=fa;
    DIACTIONFORMATW fw={0},fw_old;fw.dwSize=sizeof fw;fw_old=fw;
    assert(IDirectInput8_EnumDevicesBySemantics(a,"fixture",&fa,semantics_a,&slot,0)==E_NOTIMPL);
    assert(IDirectInput8_EnumDevicesBySemantics(w,L"fixture",&fw,semantics_w,&slot,0)==E_NOTIMPL);
    assert(!memcmp(&fa,&fa_old,sizeof fa)&&!memcmp(&fw,&fw_old,sizeof fw));
    assert(IDirectInput8_EnumDevicesBySemantics(a,NULL,NULL,semantics_a,NULL,0)==E_INVALIDARG);
    DICONFIGUREDEVICESPARAMSA ca={0},ca_old;ca.dwSize=sizeof ca;ca_old=ca;
    DICONFIGUREDEVICESPARAMSW cw={0},cw_old;cw.dwSize=sizeof cw;cw_old=cw;
    assert(IDirectInput8_ConfigureDevices(a,configure,&ca,0,&slot)==E_NOTIMPL);
    assert(IDirectInput8_ConfigureDevices(w,configure,&cw,0,&slot)==E_NOTIMPL);
    assert(!memcmp(&ca,&ca_old,sizeof ca)&&!memcmp(&cw,&cw_old,sizeof cw));
    assert(IDirectInput8_ConfigureDevices(a,NULL,NULL,0,NULL)==E_INVALIDARG);
    assert(!callbacks&&!queries&&!adds&&!releases&&GetLastError()==0x1234abcd);
    assert(IDirectInput8_Release(w)==1);
}
static DWORD WINAPI ref_thread(void *arg) {
    IDirectInput8A *a=arg;SetLastError(0x9876abcd);
    for(unsigned i=0;i<10000;i++){
        IDirectInput8W *w=NULL;IUnknown*u=NULL;
        assert(IDirectInput8_AddRef(a)>=2);assert(IDirectInput8_Release(a)>=1);
        assert(IDirectInput8_QueryInterface(a,&IID_IDirectInput8W,(void**)&w)==S_OK);
        assert(IDirectInput8_QueryInterface(w,&IID_IUnknown,(void**)&u)==S_OK&&u==(IUnknown*)a);
        IUnknown_Release(u);IDirectInput8_Release(w);
    }
    assert(GetLastError()==0x9876abcd);IDirectInput8_Release(a);return 0;
}
int main(void) {
    HMODULE dll=LoadLibraryA("dinput8.dll");assert(dll);
    create_fn create=(create_fn)GetProcAddress(dll,"DirectInput8Create");assert(create);
    unsigned matrix=creation_matrix(create);HINSTANCE instance=GetModuleHandleA(NULL);IDirectInput8A*a=NULL;
    assert(DirectInput8Create(instance,0x800,&IID_IDirectInput8A,(void**)&a,NULL)==S_OK&&a);
    identity(a);unsupported(a);
    SetLastError(0x1234abcd);
    assert(IDirectInput8_Initialize(a,instance,0x800)==S_OK);
    assert(IDirectInput8_Initialize(a,instance,0)==DIERR_NOTINITIALIZED);
    assert(IDirectInput8_Initialize(a,instance,0x700)==DIERR_BETADIRECTINPUTVERSION);
    assert(IDirectInput8_Initialize(a,instance,0x801)==DIERR_OLDDIRECTINPUTVERSION);
    assert(IDirectInput8_Initialize(a,NULL,0x800)==E_INVALIDARG);
    assert(IDirectInput8_Initialize(a,(HINSTANCE)1,0x800)==E_INVALIDARG);
    assert(IDirectInput8_Initialize(a,instance,0x800)==S_OK&&GetLastError()==0x1234abcd);
    HANDLE threads[4];for(unsigned i=0;i<4;i++){IDirectInput8_AddRef(a);threads[i]=CreateThread(NULL,0,ref_thread,a,0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(4,threads,TRUE,30000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<4;i++){DWORD code=1;assert(GetExitCodeThread(threads[i],&code)&&code==0);CloseHandle(threads[i]);}
    assert(IDirectInput8_AddRef(a)==2&&IDirectInput8_Release(a)==1&&IDirectInput8_Release(a)==0);
    for(unsigned i=0;i<1024;i++){
        IDirectInput8W*w=NULL;assert(create(instance,0x800,&IID_IDirectInput8W,(void**)&w,NULL)==S_OK);
        assert(IDirectInput8_QueryInterface(w,&IID_IDirectInput8A,(void**)&a)==S_OK);
        assert(IDirectInput8_Release(w)==1);identity(a);assert(IDirectInput8_Release(a)==0);
    }
    void*out=(void*)1;assert(create(NULL,0x800,&IID_IDirectInput8A,&out,NULL)==E_INVALIDARG&&out==NULL);
    out=(void*)1;assert(create((HINSTANCE)((uintptr_t)instance+1),0x800,&IID_IDirectInput8A,&out,NULL)==E_INVALIDARG&&out==NULL);
    assert(FreeLibrary(dll));printf("DirectInput8 root PE passed creation_matrix=%u lifecycles=1024 threads=4 iterations=40000 callbacks=0\n",matrix+1);
    return 0;
}
