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
    assert(SetEvent(event));assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    CloseHandle(thread);CloseHandle(event);return 0;
}
