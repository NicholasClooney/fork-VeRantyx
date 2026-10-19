#include <windows.h>
#include <assert.h>
#include <stdio.h>
static HANDLE event;
static DWORD WINAPI wait_thread(void *unused) {
    (void)unused;
    assert(WaitForSingleObject(event, INFINITE)==WAIT_OBJECT_0);
    return 0;
}
int main(void) {
    event=CreateEventA(NULL,TRUE,FALSE,"SECRET_WAIT_NAME");assert(event);
    HANDLE thread=CreateThread(NULL,0,wait_thread,NULL,0,NULL);assert(thread);
    Sleep(50);
    puts("waitdiag guest ready");fflush(stdout);
    Sleep(8000);
    unsigned char *peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    assert(peb && *(void **)(peb+0x20));
    assert(*(DWORD *)(*(unsigned char **)(peb+0x20)+8)==1);
    assert(SetEvent(event));assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    CloseHandle(thread);CloseHandle(event);return 0;
}
