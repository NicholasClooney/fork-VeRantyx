#include "vx.h"
#include "cpudiag.h"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
vx_process vx_proc;
static vx_image test_image;
static int use_image;
const vx_image *vx_image_at(uint64_t pc) { return use_image ? &test_image : NULL; }
unsigned vx_cpudiag_test_classify(uintptr_t pc, uintptr_t *offset);
static _Atomic int working = 1;
static _Atomic unsigned long work;
static void *busy(void *unused) { while (atomic_load(&working)) atomic_fetch_add_explicit(&work, 1, memory_order_relaxed); return NULL; }
int main(int argc, char **argv) {
#ifdef CPUDIAG_TEST_CLOCK_FAILURE
    vx_cpudiag_enable(); vx_cpudiag_start(); vx_cpudiag_finish();
    char *failure=vx_cpudiag_snapshot();assert(strstr(failure,"system_error"));free(failure);
    struct itimerval check;assert(!getitimer(ITIMER_PROF,&check));assert(!check.it_value.tv_sec&&!check.it_value.tv_usec);raise(SIGPROF);puts("clock failure passed");return 0;
#endif
    uintptr_t offset=0;
    void *kernel=dlsym(RTLD_DEFAULT,"getpid");assert(kernel);assert(vx_cpudiag_test_classify((uintptr_t)kernel,&offset)==10);
    void *objc=dlopen("/usr/lib/libobjc.A.dylib",RTLD_NOW);assert(objc);
    void *symbol=dlsym(objc,"objc_getClass");assert(symbol);assert(vx_cpudiag_test_classify((uintptr_t)symbol,&offset)==14);dlclose(objc);
    assert(!vx_cpudiag_test_classify((uintptr_t)busy,&offset));
    use_image=1;strcpy(test_image.name,"ucrtbase.dll");test_image.unix_path="/fixture/ucrtbase.dll";test_image.base=(void *)0x1000;test_image.size=0x100;
    assert(vx_cpudiag_test_classify(0x1010,&offset)==7&&offset==16);
    test_image.unloaded=1;assert(!vx_cpudiag_test_classify(0x1010,&offset));test_image.unloaded=0;
    test_image.unix_path="/fixture/SECRET_payload.dll";assert(!vx_cpudiag_test_classify(0x1010,&offset));use_image=0;
    assert(!vx_cpudiag_test_classify(1,&offset));
    const char *mode = argc > 1 ? argv[1] : "busy";
    if (!strcmp(mode,"off")) { vx_cpudiag_start(); char *r=vx_cpudiag_snapshot(); assert(!strcmp(r,"{\"status\":\"off\"}")); free(r); puts("off passed");return 0; }
    if (!strcmp(mode,"handler")) signal(SIGPROF,SIG_IGN);
    if (!strcmp(mode,"timer")) { struct itimerval it={{1,0},{1,0}}; assert(!setitimer(ITIMER_PROF,&it,NULL)); }
    if (!strcmp(mode,"profile")) setenv("VXRT_PROFILE","not-a-path",1);
    vx_cpudiag_enable();vx_cpudiag_start();vx_cpudiag_start();
    pthread_t threads[4];for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,busy,NULL));
    vx_cpudiag_finish();atomic_store(&working,0);for(int i=0;i<4;i++)pthread_join(threads[i],NULL);
    char *r=vx_cpudiag_snapshot();
    if(strcmp(mode,"busy")) assert(strstr(r,"\"status\":\"conflict\""));
    else {
        assert(strstr(r,"\"status\":\"complete\""));assert(strstr(r,"\"module\":\"runtime\""));
        struct itimerval it;assert(!getitimer(ITIMER_PROF,&it));assert(!it.it_value.tv_sec&&!it.it_value.tv_usec);
        raise(SIGPROF);char *after=vx_cpudiag_snapshot();assert(!strcmp(r,after));free(after);
    }
    struct itimerval zero={0};setitimer(ITIMER_PROF,&zero,NULL);
    puts(r);free(r);return 0;
}
