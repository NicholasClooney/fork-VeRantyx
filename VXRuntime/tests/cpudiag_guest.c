#include <windows.h>
static volatile LONG value;
static DWORD WINAPI busy(void *unused) { for (;;) InterlockedIncrement(&value); return 0; }
int main(void) { HANDLE t=CreateThread(NULL,0,busy,NULL,0,NULL); if(!t)return 1; Sleep(6000);return 0; }
