#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
static int clock_fail;
static int test_clock(clockid_t id, struct timespec *ts) {
    if (clock_fail) { errno=EIO; return -1; }
    return clock_gettime(id, ts);
}
#define clock_gettime test_clock
#include "../src/renderdiag.c"
#undef clock_gettime
static __thread DWORD last_error;
DWORD vx_get_last_error(void) { return last_error; }
void vx_set_last_error(DWORD value) { last_error=value; }
static void *work(void *unused) {
    (void)unused;
    for (int i=0;i<10000;i++) {
        errno=EACCES;last_error=123;
        vx_renderdiag_activity(RD_WORK_MAIN);
        unsigned t=vx_renderdiag_begin(RD_DEVICE);
        assert(errno==EACCES && last_error==123);
        vx_renderdiag_end(RD_DEVICE,t,(int32_t)0x80070057);
        assert(errno==EACCES && last_error==123);
    }
    return NULL;
}
int main(int argc, char **argv) {
    (void)argv;
    assert(!vx_renderdiag_begin(RD_DEVICE));assert(!counts[0].entered);
    vx_renderdiag_activity(RD_WORK_MAIN);assert(!activity[0]);
    vx_renderdiag_enable();
    unsigned outer=vx_renderdiag_begin(RD_DEVICE_SWAPCHAIN),inner=vx_renderdiag_begin(RD_DEVICE);
    vx_renderdiag_end(RD_DEVICE,inner,0);vx_renderdiag_end(RD_DEVICE_SWAPCHAIN,outer,0);
    assert(counts[0].entered==0 && counts[1].entered==1);
    pthread_t threads[4];for(int i=0;i<4;i++)pthread_create(&threads[i],NULL,work,NULL);
    for(int i=0;i<4;i++)pthread_join(threads[i],NULL);
    assert(counts[0].entered==40000 && counts[0].completed==40000 && counts[0].failure==40000);
    errno=EPERM;last_error=456;vx_renderdiag_stage(RD_NO_DRAWABLE);
    assert(errno==EPERM && last_error==456 && stages[RD_NO_DRAWABLE]==1);
    assert(!vx_renderdiag_begin(100));vx_renderdiag_stage(100);
    counts[0].entered=UINT64_MAX;unsigned t=vx_renderdiag_begin(0);vx_renderdiag_end(0,t,1);
    assert(counts[0].entered==UINT64_MAX && overflow);
    char *json=vx_renderdiag_snapshot();assert(json && strlen(json)<3072 && strstr(json,"not_instrumented"));free(json);
    depth=8;errno=EPERM;last_error=789;
    vx_renderdiag_activity(RD_WORK_MAIN);vx_renderdiag_activity(RD_WORK_PRE);
    vx_renderdiag_activity(RD_CALLBACK_ENTERED);vx_renderdiag_activity(RD_CALLBACK_COMPLETED);
    assert(depth==8 && activity[0]==40001 && activity[1]==1 && activity[2]==1 && activity[3]==1);
    assert(errno==EPERM && last_error==789);depth=0;
    clock_fail=1;json=vx_renderdiag_snapshot();
    assert(json && strstr(json,"\"process_cpu_ns\":null,\"process_cpu_valid\":false"));
    assert(errno==EPERM && last_error==789);free(json);clock_fail=0;
    for (unsigned i=0;i<RD_API_COUNT;i++) counts[i]=(counter){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX};
    for (unsigned i=0;i<RD_STAGE_COUNT;i++) stages[i]=UINT64_MAX;
    for (unsigned i=0;i<RD_ACTIVITY_COUNT;i++) activity[i]=UINT64_MAX;
    vx_renderdiag_activity(RD_WORK_MAIN);assert(activity[0]==UINT64_MAX);
    json=format_snapshot(counts,stages,activity,0,UINT64_MAX,1);
    assert(json && strlen(json)<3072 && json[strlen(json)-1]=='}');
    // Combined window metadata is under 512 bytes, including its wrapper/newline.
    assert(strlen(json)+512<4096);
    if(argc>1) {puts(json);free(json);return 0;}
    printf("maximum render JSON bytes: %zu; combined bound: %zu\n",strlen(json),strlen(json)+512);free(json);
    puts("renderdiag_host: defaultoff/nesting/40000 concurrent calls/errno/LastError/overflow passed");
}
