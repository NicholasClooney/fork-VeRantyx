#include <assert.h>
#include <stdio.h>
#include "../src/iphlp.c"
static int inside(const void *base,size_t n,uint64_t ptr,size_t len){return ptr>=(uintptr_t)base&&ptr<=(uintptr_t)base+n&&len<=((uintptr_t)base+n-ptr);}
static void check_buffer(void *b,uint32_t n,unsigned *adapters,unsigned *addresses){
    *adapters=*addresses=0;
    for(win_adapter *p=b;p;p=(void*)(uintptr_t)p->next){assert(++*adapters<256);assert(inside(b,n,(uintptr_t)p,sizeof *p));assert(p->length==448);
        assert(inside(b,n,p->name,1)&&inside(b,n,p->description,2)&&inside(b,n,p->suffix,2));if(p->friendly)assert(inside(b,n,p->friendly,2));
        uint64_t heads[]={p->unicast,p->anycast,p->multicast,p->dns,p->gateway,p->prefix};
        for(unsigned k=0;k<6;k++)for(win_node *a=(void*)(uintptr_t)heads[k];a;a=(void*)(uintptr_t)a->next){assert(++*addresses<4096);assert(inside(b,n,(uintptr_t)a,a->length));assert(inside(b,n,a->address.pointer,a->address.length));uint16_t f;memcpy(&f,(void*)(uintptr_t)a->address.pointer,2);assert((f==2&&a->address.length==16)||(f==23&&a->address.length==28));}
    }
}
static vx_ip_snapshot *fixture(void){
    vx_ip_snapshot*s=calloc(1,sizeof *s);assert(s);s->interface_count=2;
    s->interfaces[0]=(vx_ip_interface){.index=7,.mtu=1500,.type=6,.status=1,.name="fixture0"};
    s->interfaces[1]=(vx_ip_interface){.index=9,.mtu=16384,.type=24,.status=2,.name="fixture1"};
    for(unsigned k=0;k<5;k++){vx_ip_address a={.index=7,.kind=k,.source=VX_IP_MANUAL,.family=2,.prefix=24,.dad=4,.valid=UINT32_MAX,.preferred=UINT32_MAX,.lease=UINT32_MAX,.bytes={192,0,2,1}};assert(!add(s,&a));}
    vx_ip_address a={.index=7,.source=VX_IP_V6,.family=23,.scope=7,.prefix=64,.dad=3,.valid=90,.preferred=0,.bytes={0xfe,0x80}};a.bytes[15]=1;assert(!add(s,&a));a.index=9;a.scope=9;assert(!add(s,&a));return s;
}
static void test_pack(void){vx_ip_snapshot*s=fixture();uint32_t n=0;assert(vx_ip_pack(s,0,0x90,NULL,&n)==111&&n>448);void*b=malloc(n),*old=malloc(n);memset(b,0xa7,n);memcpy(old,b,n);uint32_t small=n-1;assert(vx_ip_pack(s,0,0x90,b,&small)==111&&small==n&&!memcmp(b,old,n));assert(!vx_ip_pack(s,0,0x90,b,&n));unsigned ai,an;check_buffer(b,n,&ai,&an);assert(ai==2&&an==10);
    win_adapter*p=b;win_unicast*u=(void*)(uintptr_t)p->unicast;assert(u->dad==4&&u->valid==UINT32_MAX);u=(void*)(uintptr_t)u->next;assert(u->dad==3&&u->preferred==0&&u->address.length==28);assert(*(uint32_t*)((char*)(uintptr_t)u->address.pointer+24)==7);
    for(uint32_t f=0;f<=0x1ff;f++){uint32_t z=n;if(f&0x40){memcpy(old,b,n);assert(vx_ip_pack(s,0,f,b,&z)==50&&!memcmp(b,old,n));continue;}uint32_t req=0;assert(vx_ip_pack(s,0,f,NULL,&req)==111);void*c=malloc(req);assert(!vx_ip_pack(s,0,f,c,&req));check_buffer(c,req,&ai,&an);win_adapter*q=c;assert((q->unicast==0)==!!(f&1));assert((q->anycast==0)==!!(f&2));assert((q->multicast==0)==!!(f&4));assert((q->dns==0)==!!(f&8));assert((q->friendly==0)==!!(f&0x20));assert((q->gateway!=0)==!!(f&0x80));free(c);}
    uint32_t z=n;assert(vx_ip_pack(s,99,0,b,&z)==87);assert(vx_ip_pack(s,0,0,b,NULL)==87);assert(vx_ip_pack(s,0,0x400,b,&z)==50);
    s->addresses[0].source=VX_IP_UNKNOWN;memcpy(old,b,n);assert(vx_ip_pack(s,0,0,b,&z)==50&&!memcmp(b,old,n));assert(vx_ip_pack(s,2,1,NULL,&z)==111);assert(vx_ip_pack(s,23,0,NULL,&z)==111);
    s->interface_count=s->address_count=0;assert(vx_ip_pack(s,0,0,b,&z)==232);free(s);free(old);free(b);
}
static size_t route(uint8_t*b,int family,unsigned index,unsigned last){
    struct rt_msghdr2 h={0};h.rtm_version=RTM_VERSION;h.rtm_index=index;h.rtm_flags=RTF_UP|RTF_GATEWAY;h.rtm_addrs=RTA_DST|RTA_GATEWAY|RTA_NETMASK;size_t pos=sizeof h;
    if(family==AF_INET){struct sockaddr_in d={.sin_len=16,.sin_family=AF_INET},g=d;g.sin_addr.s_addr=htonl(0xc0000200|last);memcpy(b+pos,&d,16);pos+=16;memcpy(b+pos,&g,16);pos+=16;}
    else {struct sockaddr_in6 d={.sin6_len=28,.sin6_family=AF_INET6},g=d;g.sin6_addr.s6_addr[0]=0xfe;g.sin6_addr.s6_addr[1]=0x80;g.sin6_addr.s6_addr[3]=index;g.sin6_addr.s6_addr[15]=last;memcpy(b+pos,&d,28);pos+=28;memcpy(b+pos,&g,28);pos+=28;}
    memset(b+pos,0,4);pos+=4;h.rtm_msglen=pos;memcpy(b,&h,sizeof h);return pos;
}
static void test_routes(void){vx_ip_snapshot*s=fixture();uint8_t b[1024];size_t n=route(b,AF_INET,7,1);n+=route(b+n,AF_INET,7,2);n+=route(b+n,AF_INET6,9,1);size_t before=s->address_count;assert(!vx_ip_routes(s,b,n));assert(s->address_count==before+2);assert(!vx_ip_routes(s,b,n)&&s->address_count==before+2);vx_ip_address *a=&s->addresses[s->address_count-1];assert(a->scope==9&&a->bytes[3]==0);for(size_t cut=1;cut<sizeof(struct rt_msghdr2);cut++)assert(vx_ip_routes(s,b,cut)==13);assert(vx_ip_routes(s,b,n-1)==13);uint8_t save=b[sizeof(struct rt_msghdr2)];b[sizeof(struct rt_msghdr2)]=255;assert(vx_ip_routes(s,b,n)==13);b[sizeof(struct rt_msghdr2)]=1;assert(vx_ip_routes(s,b,n)==13);b[sizeof(struct rt_msghdr2)]=save;size_t q=route(b,AF_INET,88,1);assert(vx_ip_routes(s,b,q)==1237);free(s);}
static void test_dns(void){
    vx_ip_snapshot *s=fixture();vx_ip_interface *in=&s->interfaces[0];CFStringRef server=CFSTR("192.0.2.9");CFArrayRef list=CFArrayCreate(NULL,(const void**)&server,1,&kCFTypeArrayCallBacks);
    const void *keys[]={kSCPropNetDNSServerAddresses,kSCPropNetDNSDomainName},*values[]={list,CFSTR("例.test")};
    CFDictionaryRef dict=CFDictionaryCreate(NULL,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    size_t before=s->address_count;assert(!service_dns(s,in,dict,8)&&s->address_count==before&&in->suffix[0]==0x4f8b);
    assert(!service_dns(s,in,dict,0)&&s->address_count==before+1);uint32_t n=0;assert(vx_ip_pack(s,0,8,NULL,&n)==111);void*b=malloc(n);assert(!vx_ip_pack(s,0,8,b,&n));win_adapter*p=b;assert(p->dns==0&&*(uint16_t*)(uintptr_t)p->suffix==0x4f8b);free(b);
    CFRelease(dict);CFRelease(list);free(s);
}
static void test_classification(void){
    CFStringRef ip=CFSTR("192.0.2.1");CFArrayRef list=CFArrayCreate(NULL,(const void**)&ip,1,&kCFTypeArrayCallBacks);const void *key=kSCPropNetIPv4Addresses;
    CFDictionaryRef config=CFDictionaryCreate(NULL,&key,(const void**)&list,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    vx_ip_address orig={.index=7,.family=2,.source=VX_IP_UTUN_PENDING,.bytes={192,0,2,1}},a=orig;
    classify_v4(&a,7,config,config,1,0,0,0);assert(a.source==VX_IP_MANUAL&&a.dad==4&&a.valid==UINT32_MAX);assert(classify_v4(&a,7,config,config,0,1,1,55)==50);
    a=orig;classify_v4(&a,7,config,config,0,1,1,55);assert(a.source==VX_IP_DHCP&&a.lease==55&&a.preferred==55);
    a=orig;classify_v4(&a,7,config,config,0,1,0,55);assert(a.source==VX_IP_UNKNOWN);
    a=orig;classify_v4(&a,8,config,config,1,0,0,0);assert(a.source==VX_IP_UTUN_PENDING);
    a=orig;a.bytes[3]=2;classify_v4(&a,7,config,config,1,0,0,0);assert(a.source==VX_IP_UTUN_PENDING);
    a=orig;classify_v4(&a,7,config,NULL,1,0,0,0);assert(a.source==VX_IP_UNKNOWN);
    CFDateRef start=CFDateCreate(NULL,50),end=CFDateCreate(NULL,150),future=CFDateCreate(NULL,200);uint32_t rem=19;
    assert(lease_dates(start,end,100,&rem)&&rem==50);assert(lease_dates(start,NULL,100,&rem)&&rem==UINT32_MAX);
    assert(!lease_dates(NULL,NULL,100,&rem)&&!lease_dates(future,NULL,100,&rem));assert(!lease_dates(start,end,150,&rem));
    assert(!lease_dates((CFDateRef)ip,end,100,&rem)&&!lease_dates(start,(CFDateRef)ip,100,&rem));
    CFRelease(start);CFRelease(end);CFRelease(future);CFRelease(config);CFRelease(list);
}
static void test_sources(void){uint32_t r=17,t=18;assert(!vx_ip_lifetime(100,50,150,&r,&t)&&r==50&&t==100);double bad[][3]={{100,101,150},{100,50,99},{100,50,100.5},{100,NAN,150},{100,50,INFINITY},{100,50,4294967395.0}};for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){r=17;t=18;assert(vx_ip_lifetime(bad[i][0],bad[i][1],bad[i][2],&r,&t)==50&&r==17&&t==18);}assert(remaining(100,99,5)==0&&remaining(100,0,UINT32_MAX)==UINT32_MAX);
    vx_ip_interface in={.index=7,.type=1,.name="utun123"};struct sockaddr_in dst={.sin_len=16,.sin_family=AF_INET,.sin_addr.s_addr=1};struct ifaddrs p={.ifa_name=in.name,.ifa_flags=IFF_POINTOPOINT,.ifa_dstaddr=(void*)&dst};assert(utun_candidate(&in,&p));p.ifa_flags=0;assert(!utun_candidate(&in,&p));p.ifa_flags=IFF_POINTOPOINT;dst.sin_addr.s_addr=0;assert(!utun_candidate(&in,&p));dst.sin_addr.s_addr=1;strcpy(in.name,"utun12x");assert(!utun_candidate(&in,&p));strcpy(in.name,"other0");assert(!utun_candidate(&in,&p));
    CFStringRef text=CFSTR("192.0.2.1");CFArrayRef ips=CFArrayCreate(NULL,(const void**)&text,1,&kCFTypeArrayCallBacks);vx_ip_address a={.bytes={192,0,2,1}};assert(array_has_address(ips,&a));a.bytes[3]=2;assert(!array_has_address(ips,&a));CFRelease(ips);
    struct sockaddr_in mask={.sin_len=16,.sin_family=AF_INET,.sin_addr.s_addr=htonl(0xffffff00)};assert(prefix_length((void*)&mask,AF_INET)==24);mask.sin_addr.s_addr=htonl(0xff00ff00);assert(prefix_length((void*)&mask,AF_INET)==-1);
    struct sockaddr_in6 v6={.sin6_len=28,.sin6_family=AF_INET6};v6.sin6_addr.s6_addr[0]=0xfe;v6.sin6_addr.s6_addr[1]=0x80;v6.sin6_addr.s6_addr[3]=7;a=(vx_ip_address){0};assert(!address(&a,(void*)&v6,7)&&a.scope==7&&a.bytes[3]==0);v6.sin6_scope_id=8;assert(address(&a,(void*)&v6,7)==13);
    struct ifaddrs x={.ifa_name="fixture",.ifa_addr=(void*)&dst,.ifa_netmask=(void*)&mask,.ifa_flags=1},y=x;assert(same_topology(&x,&y));y.ifa_flags=2;assert(!same_topology(&x,&y));y=x;y.ifa_dstaddr=(void*)&dst;assert(!same_topology(&x,&y));
}
int main(int argc,char**argv){(void)argv;if(argc>1){uint32_t n=0,rc=vx_get_adapters_addresses(0,0x80,NULL,NULL,&n);printf("host_size_result=%u\n",rc);if(rc!=111)return 1;void*b=malloc(n);rc=vx_get_adapters_addresses(0,0x80,NULL,b,&n);if(rc){printf("host_result=%u\n",rc);return 1;}unsigned ai,an;check_buffer(b,n,&ai,&an);printf("host_result=0 adapters=%u address_nodes=%u\n",ai,an);free(b);return 0;}test_pack();test_routes();test_sources();test_classification();test_dns();puts("iphlp fixtures passed");return 0;}
