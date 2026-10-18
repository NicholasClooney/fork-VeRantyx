#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
_Static_assert(sizeof(IP_ADAPTER_ADDRESSES)==448,"adapter ABI");
_Static_assert(sizeof(IP_ADAPTER_UNICAST_ADDRESS)==64,"unicast ABI");
_Static_assert(offsetof(IP_ADAPTER_ADDRESSES,FirstGatewayAddress)==208,"gateway ABI");
static int within(void*b,ULONG n,void*p,size_t z){return (uintptr_t)p>=(uintptr_t)b&&(uintptr_t)p<=(uintptr_t)b+n&&z<=(uintptr_t)b+n-(uintptr_t)p;}
int main(void){ULONG n=0;SetLastError(0x12345678);assert(GetAdaptersAddresses(99,0,NULL,NULL,&n)==ERROR_INVALID_PARAMETER);assert(GetLastError()==0x12345678);assert(GetAdaptersAddresses(0,0,NULL,NULL,NULL)==ERROR_INVALID_PARAMETER);assert(GetAdaptersAddresses(0,0x80000000,NULL,NULL,&n)==ERROR_NOT_SUPPORTED);
    ULONG rc=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_INCLUDE_GATEWAYS|GAA_FLAG_INCLUDE_PREFIX,NULL,NULL,&n);if(rc!=ERROR_BUFFER_OVERFLOW){printf("adapter PE host result=%lu\n",rc);return 1;}
    void*b=malloc(n),*copy=malloc(n);memset(b,0xa3,n);memcpy(copy,b,n);ULONG small=1;assert(GetAdaptersAddresses(0,0x90,NULL,b,&small)==ERROR_BUFFER_OVERFLOW);assert(!memcmp(b,copy,n));free(copy);
    for(int tries=0;tries<3;tries++){b=realloc(b,n);rc=GetAdaptersAddresses(0,0x90,NULL,b,&n);if(rc!=ERROR_BUFFER_OVERFLOW)break;}assert(rc==NO_ERROR);assert(GetLastError()==0x12345678);
    unsigned adapters=0,nodes=0;for(IP_ADAPTER_ADDRESSES*p=b;p;p=p->Next){assert(++adapters<256);assert(within(b,n,p,sizeof *p)&&p->Length==sizeof *p);assert(within(b,n,p->AdapterName,1)&&within(b,n,p->Description,2)&&within(b,n,p->FriendlyName,2)&&within(b,n,p->DnsSuffix,2));
        IP_ADAPTER_DNS_SERVER_ADDRESS *lists[]={(void*)p->FirstUnicastAddress,(void*)p->FirstAnycastAddress,(void*)p->FirstMulticastAddress,p->FirstDnsServerAddress,(void*)p->FirstGatewayAddress,(void*)p->FirstPrefix};
        for(unsigned k=0;k<6;k++)for(IP_ADAPTER_DNS_SERVER_ADDRESS*a=lists[k];a;a=a->Next){assert(++nodes<4096);assert(within(b,n,a,a->Length)&&within(b,n,a->Address.lpSockaddr,a->Address.iSockaddrLength));assert(a->Address.lpSockaddr->sa_family==AF_INET||a->Address.lpSockaddr->sa_family==AF_INET6);}
    }printf("adapter PE ABI passed adapters=%u nodes=%u\n",adapters,nodes);free(b);return 0;}
