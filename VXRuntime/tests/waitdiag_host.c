#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/waitdiag.c"
static __thread DWORD last_error;
static __thread unsigned error_accesses;
vx_process vx_proc;
static vx_image mock_image;
DWORD vx_get_last_error(void) { error_accesses++; return last_error; }
void vx_set_last_error(DWORD value) { error_accesses++; last_error=value; }
const vx_image *vx_image_at(uint64_t pc) {
    return pc >= (uintptr_t)mock_image.base && pc-(uintptr_t)mock_image.base < mock_image.size ? &mock_image : NULL;
}
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<2000;i++) {
        errno=EACCES; last_error=123;
        VX_WAITDIAG_SCOPE("WaitForSingleObject", 0x1234, 1);
        vx_waitdiag_phase("test_wait"); vx_waitdiag_result(&wd_scope,258);
        assert(errno==EACCES && last_error==123);
    }
    return NULL;
}
static void *one_thread(void *unused) { (void)unused; VX_WAITDIAG_SCOPE("Sleep",0,0); return NULL; }
int main(int argc, char **argv) {
    if(argc>1 && !strcmp(argv[1],"conflict")) {
        setenv("VXRT_TRACE_CALLS","1",1); vx_waitdiag_enable(); assert(!error_accesses);
        assert(!vx_waitdiag_begin("Sleep",0,0,0).slot);
        char *json=vx_waitdiag_snapshot(); assert(strstr(json,"\"status\":\"conflict\"")); free(json); return 0;
    }
    assert(!vx_waitdiag_begin("Sleep",0,0,0).slot);
    char *json=vx_waitdiag_snapshot(); assert(json && strstr(json,"\"status\":\"off\"")); free(json);
    errno=EPERM;last_error=456;unsigned before_enable=error_accesses;vx_waitdiag_enable();
    assert(error_accesses==before_enable && errno==EPERM && last_error==456);
    vx_waitdiag_scope outer=vx_waitdiag_begin("WaitForSingleObject",0x1234,1,0x1010);
    vx_waitdiag_phase("test_outer");vx_waitdiag_present();
    vx_waitdiag_scope inner=vx_waitdiag_begin("Sleep",0,1,0x2020);
    vx_waitdiag_phase("test_inner");
    json=vx_waitdiag_snapshot(); assert(json && strstr(json,"test_outer") && strstr(json,"test_inner") && strstr(json,"\"active\":true")); free(json);
    vx_waitdiag_end(&inner);
    json=vx_waitdiag_snapshot(); assert(json && strstr(json,"test_outer") && !strstr(json,"test_inner")); free(json);
    vx_waitdiag_result(&outer,-1);vx_waitdiag_end(&outer);
    json=vx_waitdiag_snapshot(); assert(json && strstr(json,"\"active\":false") && strstr(json,"\"result\":-1") && strstr(json,"\"last_present_thread\":1"));free(json);
    assert(errno==EPERM && last_error==456);
    mock_image=(vx_image){.base=(void *)0x1000,.size=0x100}; vx_proc.image_base=mock_image.base;vx_proc.image_size=mock_image.size;
    uintptr_t offset;assert(!strcmp(classify(0x1010,&offset),"main_image") && offset==0x10);
    mock_image.unloaded=1;assert(!strcmp(classify(0x1010,&offset),"unknown") && !offset); mock_image.unloaded=0;
    vx_proc.image_base=NULL;strcpy(mock_image.name,"vrclient_x64.dll");mock_image.unix_path="/private/vrclient_x64.dll";
    assert(!strcmp(classify(0x1010,&offset),"vrclient_x64") && offset==0x10);
    mock_image.unix_path="/private/private-user-name.dll";assert(!strcmp(classify(0x1010,&offset),"unknown") && !offset);
    pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));
    for(unsigned i=0;i<100;i++) {json=vx_waitdiag_snapshot();assert(json && strlen(json)<131072 && !strstr(json,"private-user"));free(json);}
    for(unsigned i=0;i<4;i++)pthread_join(threads[i],NULL);
    for(unsigned i=1;i<5;i++)assert(slots[i].entered==2000 && slots[i].completed==2000 && !slots[i].depth);
    pthread_mutex_lock(&objects_lock);
    errno=EPERM;last_error=456;
    vx_waitdiag_scope contended=vx_waitdiag_begin("SetEvent",0x9999,0,0);
    assert(contended.slot && slots[contended.slot-1].frames[0].object==0);
    vx_waitdiag_end(&contended);
    assert(errno==EPERM && last_error==456);pthread_mutex_unlock(&objects_lock);
    vx_waitdiag_scope nested[12];for(unsigned i=0;i<12;i++)nested[i]=vx_waitdiag_begin("Sleep",0,0,0);
    json=vx_waitdiag_snapshot();assert(strstr(json,"\"truncated\":true"));free(json);
    for(unsigned i=12;i;i--)vx_waitdiag_end(&nested[i-1]);assert(!nesting);
    for(unsigned i=0;i<OBJECTS+1;i++)token(0x100000+i);
    assert(object_count==OBJECTS && token(0x1234)==1);
    pthread_mutex_lock(&slots[0].lock);json=vx_waitdiag_snapshot();assert(json && strstr(json,"\"overflow\":true"));free(json);pthread_mutex_unlock(&slots[0].lock);
    for(unsigned i=0;i<THREADS+2;i++){pthread_t t;assert(!pthread_create(&t,NULL,one_thread,NULL));pthread_join(t,NULL);}
    json=vx_waitdiag_snapshot();assert(json && strlen(json)<131072 && strstr(json,"\"overflow\":true"));free(json);
    for(unsigned i=0;i<THREADS;i++) {
        slots[i].depth=DEPTH;slots[i].entered=UINT64_MAX;slots[i].completed=UINT64_MAX;
        for(unsigned j=0;j<DEPTH;j++)slots[i].frames[j]=(frame){.api="WaitForMultipleObjectsEx",.phase="objects_condition_wait",.started=UINT64_MAX,.object=OBJECTS,.has_result=1,.result=INT64_MIN};
    }
    json=vx_waitdiag_snapshot();assert(json && strlen(json)<131072 && json[strlen(json)-1]=='}');
    if(argc>1 && !strcmp(argv[1],"max")) puts(json);
    free(json);
    if(argc==1) puts("waitdiag core: PASS");return 0;
}
