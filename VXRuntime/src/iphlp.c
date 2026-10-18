// Read-only Darwin snapshots, normalized before any caller-owned memory is touched.
#include "iphlp.h"
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCDynamicStoreCopyDHCPInfo.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <arpa/inet.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#define OK 0u
#define NOMEM 8u
#define BAD_DATA 13u
#define UNSUPPORTED 50u
#define BAD_PARAM 87u
#define BUFFER_OVERFLOW 111u
#define NO_DATA 232u
#define RETRY 1237u
#define SUPPORTED_FLAGS 0x1ffu
// INCLUDE_WINS_INFO and INCLUDE_ALL_COMPARTMENTS require sources this module does not expose.
#define UNSUPPORTED_FLAGS (0x40u | 0x200u | 0x400u)

typedef struct { uint64_t pointer; int32_t length; uint32_t pad; } win_socket;
typedef struct {
    uint32_t length, flags; uint64_t next; win_socket address;
    uint32_t prefix_origin, suffix_origin, dad, valid, preferred, lease;
    uint8_t prefix, pad[7];
} win_unicast;
typedef struct { uint32_t length, flags; uint64_t next; win_socket address; } win_node;
typedef struct { win_node node; uint32_t prefix, pad; } win_prefix;
typedef struct {
    uint32_t length, index; uint64_t next, name, unicast, anycast, multicast, dns;
    uint64_t suffix, description, friendly;
    uint8_t physical[8]; uint32_t physical_length, flags, mtu, type, status, index6, zones[16];
    uint64_t prefix, tx, rx, wins, gateway;
    uint32_t metric4, metric6; uint64_t luid; win_socket dhcp4;
    uint32_t compartment; uint8_t guid[16]; uint32_t connection, tunnel;
    win_socket dhcp6; uint8_t duid[130]; uint32_t duid_length, iaid; uint64_t suffixes;
} win_adapter;
_Static_assert(sizeof(void *) == 8, "64-bit host required");
_Static_assert(sizeof(win_unicast) == 64, "unicast ABI");
_Static_assert(offsetof(win_adapter, gateway) == 208, "gateway ABI");
_Static_assert(offsetof(win_adapter, type) == 100, "adapter ABI");
_Static_assert(sizeof(win_adapter) == 448, "adapter ABI");

static vx_ip_interface *interface(vx_ip_snapshot *s, uint32_t index) {
    for (size_t i=0;i<s->interface_count;i++) if(s->interfaces[i].index==index) return &s->interfaces[i];
    return NULL;
}
static uint32_t add(vx_ip_snapshot *s, const vx_ip_address *a) {
    for(size_t i=0;i<s->address_count;i++) {
        vx_ip_address *b=&s->addresses[i];
        if(b->index==a->index && b->kind==a->kind && b->family==a->family && b->scope==a->scope && !memcmp(b->bytes,a->bytes,16)) return OK;
    }
    if(s->address_count==VX_IP_MAX) return NOMEM;
    s->addresses[s->address_count++]=*a; return OK;
}
static uint32_t address(vx_ip_address *a, const struct sockaddr *sa, uint32_t index) {
    a->index=index;
    if(sa->sa_family==AF_INET && sa->sa_len>=sizeof(struct sockaddr_in)) {
        a->family=2; memcpy(a->bytes,&((const struct sockaddr_in*)sa)->sin_addr,4); return OK;
    }
    if(sa->sa_family==AF_INET6 && sa->sa_len>=sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *v=(const void*)sa; a->family=23;
        memcpy(a->bytes,&v->sin6_addr,16); a->scope=v->sin6_scope_id;
        if(IN6_IS_ADDR_LINKLOCAL(&v->sin6_addr) || IN6_IS_ADDR_MC_LINKLOCAL(&v->sin6_addr)) {
            uint32_t embedded=((uint32_t)a->bytes[2]<<8)|a->bytes[3];
            if(a->scope && embedded && a->scope!=embedded) return BAD_DATA;
            if(!a->scope) a->scope=embedded?embedded:index;
            a->bytes[2]=a->bytes[3]=0;
        }
        return OK;
    }
    return BAD_DATA;
}
uint32_t vx_ip_lifetime(double now, double start, double end, uint32_t *remaining, uint32_t *lease) {
    // Future starts can indicate a clock jump or an unrelated lease. Never wrap or invent infinity.
    if(!isfinite(now)||!isfinite(start)||!isfinite(end)||start>now||end-now<1||end<=start||end-start>=UINT32_MAX) return UNSUPPORTED;
    *remaining=(uint32_t)(end-now); *lease=(uint32_t)(end-start); return OK;
}
static uint32_t remaining(time_t now, time_t end, uint32_t duration) {
    if(duration==UINT32_MAX) return UINT32_MAX;
    if(end<=now) return 0;
    uint64_t n=(uint64_t)(end-now); return n>=UINT32_MAX?UINT32_MAX-1:(uint32_t)n;
}
static int prefix_length(const struct sockaddr *mask, int family) {
    if(!mask) return -1;
    const uint8_t *p; int n;
    if(family==AF_INET){p=(const void*)&((const struct sockaddr_in*)mask)->sin_addr;n=4;}
    else {p=(const void*)&((const struct sockaddr_in6*)mask)->sin6_addr;n=16;}
    int bits=0,zero=0; for(int i=0;i<n;i++)for(int j=7;j>=0;j--){if(p[i]&(1<<j)){if(zero)return -1;bits++;}else zero=1;}
    return bits;
}
// Names here come from the kernel, whose utun driver allocates utun%d / IFT_OTHER.
// This is a bounded session-address mapping, not a claim about a VPN server's hidden leases.
static int utun_candidate(const vx_ip_interface *in, const struct ifaddrs *p) {
    if(in->type!=IFT_OTHER || !(p->ifa_flags&IFF_POINTOPOINT) || strcmp(in->name,p->ifa_name) ||
       strncmp(in->name,"utun",4) || !in->name[4] || !p->ifa_dstaddr ||
       p->ifa_dstaddr->sa_family!=AF_INET || p->ifa_dstaddr->sa_len<sizeof(struct sockaddr_in))return 0;
    for(const char *c=in->name+4;*c;c++)if(*c<'0'||*c>'9')return 0;
    return ((struct sockaddr_in*)p->ifa_dstaddr)->sin_addr.s_addr!=0;
}
static uint32_t collect_addresses(vx_ip_snapshot *s, struct ifaddrs *list, time_t now) {
    for(struct ifaddrs *p=list;p;p=p->ifa_next) {
        if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_LINK)continue;
        const struct sockaddr_dl *dl=(const void*)p->ifa_addr;
        if(interface(s,dl->sdl_index))continue;
        if(s->interface_count==VX_IF_MAX||strlen(p->ifa_name)>=16||dl->sdl_alen>8||!p->ifa_data) return UNSUPPORTED;
        if((size_t)offsetof(struct sockaddr_dl,sdl_data)+dl->sdl_nlen+dl->sdl_alen>dl->sdl_len)return BAD_DATA;
        vx_ip_interface *i=&s->interfaces[s->interface_count++];struct if_data *d=p->ifa_data;
        i->index=dl->sdl_index;strlcpy(i->name,p->ifa_name,sizeof i->name);i->mtu=d->ifi_mtu;i->type=d->ifi_type;
        i->status=(p->ifa_flags&IFF_UP)?((p->ifa_flags&IFF_RUNNING)?1:5):2;i->speed=d->ifi_baudrate;
        i->physical_length=dl->sdl_alen;memcpy(i->physical,LLADDR(dl),dl->sdl_alen);
        if(!(p->ifa_flags&IFF_MULTICAST))i->flags|=0x10;
    }
    int fd=socket(AF_INET6,SOCK_DGRAM,0);uint32_t rc=OK;
    for(struct ifaddrs *p=list;p&&rc==OK;p=p->ifa_next) {
        if(!p->ifa_addr||(p->ifa_addr->sa_family!=AF_INET&&p->ifa_addr->sa_family!=AF_INET6))continue;
        uint32_t index=if_nametoindex(p->ifa_name);vx_ip_interface *i=interface(s,index);
        if(!i||strcmp(i->name,p->ifa_name)){rc=RETRY;break;}
        vx_ip_address a={0};rc=address(&a,p->ifa_addr,index);if(rc)break;
        int prefix=prefix_length(p->ifa_netmask,p->ifa_addr->sa_family);if(prefix<0){rc=BAD_DATA;break;}a.prefix=prefix;
        if(a.family==2){i->flags|=0x80;if(utun_candidate(i,p))a.source=VX_IP_UTUN_PENDING;if((p->ifa_flags&IFF_LOOPBACK)&&a.bytes[0]==127){a.source=VX_IP_LOOPBACK;a.dad=4;a.valid=a.preferred=a.lease=UINT32_MAX;a.prefix_origin=a.suffix_origin=1;}}
        else {
            i->flags|=0x100;struct in6_ifreq q={0};strlcpy(q.ifr_name,p->ifa_name,sizeof q.ifr_name);memcpy(&q.ifr_ifru.ifru_addr,p->ifa_addr,sizeof(struct sockaddr_in6));
            if(fd<0||ioctl(fd,SIOCGIFAFLAG_IN6,&q)){rc=RETRY;break;}int f=q.ifr_ifru.ifru_flags6;
            memset(&q,0,sizeof q);strlcpy(q.ifr_name,p->ifa_name,sizeof q.ifr_name);memcpy(&q.ifr_ifru.ifru_addr,p->ifa_addr,sizeof(struct sockaddr_in6));
            if(ioctl(fd,SIOCGIFALIFETIME_IN6,&q)){rc=RETRY;break;}
            a.source=VX_IP_V6;a.kind=(f&IN6_IFF_ANYCAST)?VX_IP_ANYCAST:VX_IP_UNICAST;
            a.dad=(f&IN6_IFF_DUPLICATED)?2:(f&IN6_IFF_TENTATIVE)?1:(f&IN6_IFF_DETACHED)?0:(f&IN6_IFF_DEPRECATED)?3:4;
            a.valid=remaining(now,q.ifr_ifru.ifru_lifetime.ia6t_expire,q.ifr_ifru.ifru_lifetime.ia6t_vltime);
            a.preferred=remaining(now,q.ifr_ifru.ifru_lifetime.ia6t_preferred,q.ifr_ifru.ifru_lifetime.ia6t_pltime);
            a.lease=a.valid;a.prefix_origin=(f&IN6_IFF_DYNAMIC)?3:(f&IN6_IFF_AUTOCONF)?4:1;
            a.suffix_origin=(f&IN6_IFF_TEMPORARY)?5:(f&IN6_IFF_DYNAMIC)?3:(f&IN6_IFF_AUTOCONF)?4:1;
        }
        rc=add(s,&a);
    }
    if(fd>=0)close(fd);return rc;
}
static CFTypeRef value(CFTypeRef d, CFStringRef key) {
    return d&&CFGetTypeID(d)==CFDictionaryGetTypeID()?CFDictionaryGetValue(d,key):NULL;
}
static int text_value(CFTypeRef v, char *out, size_t n) {
    return v&&CFGetTypeID(v)==CFStringGetTypeID()&&CFStringGetCString(v,out,n,kCFStringEncodingUTF8);
}
static int array_has_address(CFTypeRef list, const vx_ip_address *a) {
    if(!list||CFGetTypeID(list)!=CFArrayGetTypeID())return 0;
    for(CFIndex j=0;j<CFArrayGetCount(list);j++){
        char b[64];uint8_t ip[16];if(text_value(CFArrayGetValueAtIndex(list,j),b,sizeof b)&&inet_pton(AF_INET,b,ip)==1&&!memcmp(ip,a->bytes,4))return 1;
    }return 0;
}
static int lease_dates(CFDateRef start, CFDateRef end, double now, uint32_t *rem) {
    if(!start||CFGetTypeID(start)!=CFDateGetTypeID()||!isfinite(now))return 0;
    double begin=CFDateGetAbsoluteTime(start);uint32_t total;
    if(end)return CFGetTypeID(end)==CFDateGetTypeID()&&vx_ip_lifetime(now,begin,CFDateGetAbsoluteTime(end),rem,&total)==OK;
    if(!isfinite(begin)||begin>now)return 0;
    *rem=UINT32_MAX;return 1;
}
static uint32_t classify_v4(vx_ip_address *a, uint32_t index, CFTypeRef state, CFTypeRef config, int manual, int dhcp, int valid_lease, uint32_t rem) {
    if(a->index!=index||a->family!=2||(a->source!=VX_IP_UNKNOWN&&a->source!=VX_IP_UTUN_PENDING&&a->source!=VX_IP_MANUAL&&a->source!=VX_IP_DHCP)||!array_has_address(value(state,kSCPropNetIPv4Addresses),a))return OK;
    vx_ip_address previous=*a;
    // A known service prevents the session fallback, even when its lease is invalid.
    a->source=VX_IP_UNKNOWN;
    if(manual&&array_has_address(value(config,kSCPropNetIPv4Addresses),a)){
        a->source=VX_IP_MANUAL;a->valid=a->preferred=a->lease=UINT32_MAX;a->prefix_origin=a->suffix_origin=1;
    }else if(dhcp&&valid_lease){
        a->source=VX_IP_DHCP;a->valid=a->preferred=a->lease=rem;a->prefix_origin=a->suffix_origin=3;
    }
    // Published, source-associated IPv4 is usable on Darwin; this does not assert that Windows DAD ran.
    if(a->source!=VX_IP_UNKNOWN)a->dad=4;
    if((previous.source==VX_IP_MANUAL||previous.source==VX_IP_DHCP)&&
       (previous.source!=a->source||previous.valid!=a->valid||previous.preferred!=a->preferred))return UNSUPPORTED;
    return OK;
}
static uint32_t service_dns(vx_ip_snapshot *s, vx_ip_interface *in, CFTypeRef dict, uint32_t flags) {
    CFTypeRef servers=value(dict,kSCPropNetDNSServerAddresses);
    if(servers&&CFGetTypeID(servers)!=CFArrayGetTypeID())return BAD_DATA;
    if(servers&&!(flags&8))for(CFIndex z=0;z<CFArrayGetCount(servers);z++){
        char b[128];vx_ip_address a={.index=in->index,.kind=VX_IP_DNS};
        if(!text_value(CFArrayGetValueAtIndex(servers,z),b,sizeof b))return BAD_DATA;
        char *zone=strchr(b,'%');if(zone){*zone++=0;if(if_nametoindex(zone)!=in->index)return UNSUPPORTED;a.scope=in->index;}
        if(inet_pton(AF_INET,b,a.bytes)==1)a.family=2;
        else if(inet_pton(AF_INET6,b,a.bytes)==1){a.family=23;if(a.bytes[0]==0xfe&&(a.bytes[1]&0xc0)==0x80)a.scope=in->index;}
        else return BAD_DATA;
        uint32_t rc=add(s,&a);if(rc)return rc;
    }
    CFTypeRef suffix=value(dict,kSCPropNetDNSDomainName);
    if(suffix){
        if(CFGetTypeID(suffix)!=CFStringGetTypeID()||CFStringGetLength(suffix)>255)return UNSUPPORTED;
        uint16_t temp[256]={0};CFStringGetCharacters(suffix,CFRangeMake(0,CFStringGetLength(suffix)),temp);
        if(in->suffix[0]&&memcmp(in->suffix,temp,sizeof temp))return UNSUPPORTED;
        memcpy(in->suffix,temp,sizeof temp);
    }
    return OK;
}
static uint32_t collect_configuration(vx_ip_snapshot *s, double now, uint32_t flags) {
    SCDynamicStoreRef store=SCDynamicStoreCreate(NULL,CFSTR("VXRuntime adapter snapshot"),NULL,NULL);
    if(!store)return UNSUPPORTED;
    const void *patterns[]={CFSTR("State:/Network/Service/[^/]+/(IPv4|IPv6|DNS)"),CFSTR("Setup:/Network/Service/[^/]+/IPv4")};
    CFArrayRef ps=CFArrayCreate(NULL,patterns,2,&kCFTypeArrayCallBacks);
    CFDictionaryRef data=ps?SCDynamicStoreCopyMultiple(store,NULL,ps):NULL;
    if(!data){if(ps)CFRelease(ps);CFRelease(store);return UNSUPPORTED;}
    CFIndex count=CFDictionaryGetCount(data);const void **keys=calloc(count?count:1,sizeof(void*)),**vals=calloc(count?count:1,sizeof(void*));
    uint32_t rc=OK;if(!keys||!vals){rc=NOMEM;goto done;}
    CFDictionaryGetKeysAndValues(data,keys,vals);
    for(CFIndex j=0;j<count&&rc==OK;j++) {
        if(!CFStringHasPrefix(keys[j],CFSTR("State:")))continue;
        if(CFStringHasSuffix(keys[j],CFSTR("/IPv4"))) {
            char name[16];if(!text_value(value(vals[j],kSCPropInterfaceName),name,sizeof name))continue;
            uint32_t index=if_nametoindex(name);vx_ip_interface *in=interface(s,index);if(!in){rc=RETRY;break;}
            CFStringRef tail=CFStringCreateWithSubstring(NULL,keys[j],CFRangeMake(6,CFStringGetLength(keys[j])-6));
            CFStringRef setup=CFStringCreateWithFormat(NULL,NULL,CFSTR("Setup:%@"),tail);CFRelease(tail);
            CFTypeRef config=CFDictionaryGetValue(data,setup),method=value(config,kSCPropNetIPv4ConfigMethod);CFRelease(setup);
            int manual=method&&CFEqual(method,kSCValNetIPv4ConfigMethodManual),dhcp=method&&CFEqual(method,kSCValNetIPv4ConfigMethodDHCP);
            uint32_t rem=0;int valid_lease=0;
            if(dhcp){CFStringRef id=CFStringCreateWithSubstring(NULL,keys[j],CFRangeMake(23,CFStringGetLength(keys[j])-28));CFDictionaryRef info=SCDynamicStoreCopyDHCPInfo(store,id);CFRelease(id);
                if(info){valid_lease=lease_dates(DHCPInfoGetLeaseStartTime(info),DHCPInfoGetLeaseExpirationTime(info),now,&rem);CFRelease(info);}in->flags|=4;}
            for(size_t k=0;k<s->address_count&&!rc;k++)rc=classify_v4(&s->addresses[k],index,vals[j],config,manual,dhcp,valid_lease,rem);
        }
        if(CFStringHasSuffix(keys[j],CFSTR("/DNS"))) {
            CFStringRef base=CFStringCreateWithSubstring(NULL,keys[j],CFRangeMake(0,CFStringGetLength(keys[j])-3));
            CFStringRef k4=CFStringCreateWithFormat(NULL,NULL,CFSTR("%@IPv4"),base),k6=CFStringCreateWithFormat(NULL,NULL,CFSTR("%@IPv6"),base);CFRelease(base);
            CFTypeRef n4=value(CFDictionaryGetValue(data,k4),kSCPropInterfaceName),n6=value(CFDictionaryGetValue(data,k6),kSCPropInterfaceName);CFRelease(k4);CFRelease(k6);
            char name[16];if((n4&&n6&&!CFEqual(n4,n6))||!text_value(n4?n4:n6,name,sizeof name)){rc=UNSUPPORTED;break;}
            uint32_t index=if_nametoindex(name);vx_ip_interface *in=interface(s,index);if(!in){rc=RETRY;break;}
            rc=service_dns(s,in,vals[j],flags);
        }
    }
    for(size_t k=0;k<s->address_count;k++){vx_ip_address *a=&s->addresses[k];if(a->source==VX_IP_UTUN_PENDING){a->source=VX_IP_UTUN;a->dad=4;a->valid=a->preferred=a->lease=UINT32_MAX;}}
done:
    if(!rc){CFDictionaryRef again=SCDynamicStoreCopyMultiple(store,NULL,ps);if(!again||!CFEqual(data,again))rc=RETRY;if(again)CFRelease(again);}
    free(keys);free(vals);CFRelease(ps);CFRelease(data);CFRelease(store);return rc;
}

uint32_t vx_ip_routes(vx_ip_snapshot *s, const void *buffer, size_t length) {
    const uint8_t *b=buffer;size_t offset=0;
    while(offset<length){
        if(length-offset<sizeof(struct rt_msghdr2))return BAD_DATA;
        struct rt_msghdr2 h;memcpy(&h,b+offset,sizeof h);
        if(h.rtm_msglen<sizeof h||h.rtm_msglen>length-offset||h.rtm_version!=RTM_VERSION||(h.rtm_addrs&~((1<<RTAX_MAX)-1)))return BAD_DATA;
        const struct sockaddr *sa[RTAX_MAX]={0};size_t pos=sizeof h;
        for(int k=0;k<RTAX_MAX;k++)if(h.rtm_addrs&(1<<k)){
            if(h.rtm_msglen-pos<2)return BAD_DATA;const uint8_t *p=b+offset+pos;
            if(p[0]==1)return BAD_DATA;
            size_t step=p[0]?((p[0]+3u)&~3u):4;if(step>h.rtm_msglen-pos)return BAD_DATA;
            sa[k]=(const void*)p;pos+=step;
        }
        if(pos!=h.rtm_msglen)return BAD_DATA;
        if((h.rtm_flags&(RTF_UP|RTF_GATEWAY))==(RTF_UP|RTF_GATEWAY)&&!(h.rtm_flags&(RTF_HOST|RTF_REJECT|RTF_BLACKHOLE))&&sa[RTAX_DST]&&sa[RTAX_GATEWAY]){
            vx_ip_address dst={0},gw={.kind=VX_IP_GATEWAY};const struct sockaddr *d=sa[RTAX_DST],*g=sa[RTAX_GATEWAY];
            if((d->sa_family==AF_INET||d->sa_family==AF_INET6)&&g->sa_family==d->sa_family){
                uint32_t rc=address(&dst,d,h.rtm_index);if(rc)return rc;rc=address(&gw,g,h.rtm_index);if(rc)return rc;
                int zero=1;for(int k=0;k<16;k++)if(dst.bytes[k])zero=0;
                const struct sockaddr *m=sa[RTAX_NETMASK];if(m){size_t begin=d->sa_family==AF_INET?4:8;const uint8_t *p=(const void*)m;for(size_t k=begin;k<m->sa_len;k++)if(p[k])zero=0;}
                if(zero){if(!interface(s,h.rtm_index))return RETRY;rc=add(s,&gw);if(rc)return rc;}
            }
        }
        offset+=h.rtm_msglen;
    }return OK;
}
static uint32_t collect_routes(vx_ip_snapshot *s) {
    int mib[]={CTL_NET,PF_ROUTE,0,0,NET_RT_DUMP2,0};
    for(int attempt=0;attempt<3;attempt++){
        size_t n=0;if(sysctl(mib,6,NULL,&n,NULL,0))return UNSUPPORTED;if(n>16*1024*1024)return NOMEM;
        void *b=malloc(n?n:1);if(!b)return NOMEM;
        if(sysctl(mib,6,b,&n,NULL,0)){int e=errno;free(b);if(e==ENOMEM)continue;return UNSUPPORTED;}
        uint32_t rc=vx_ip_routes(s,b,n);free(b);return rc;
    }return RETRY;
}
static uint32_t collect_multicast(vx_ip_snapshot *s) {
    struct ifmaddrs *list=NULL;if(getifmaddrs(&list))return UNSUPPORTED;uint32_t rc=OK;
    for(struct ifmaddrs *p=list;p&&!rc;p=p->ifma_next){
        if(!p->ifma_addr||(p->ifma_addr->sa_family!=AF_INET&&p->ifma_addr->sa_family!=AF_INET6))continue;
        if(!p->ifma_name||p->ifma_name->sa_family!=AF_LINK){rc=BAD_DATA;break;}
        uint32_t index=((struct sockaddr_dl*)p->ifma_name)->sdl_index;if(!interface(s,index)){rc=RETRY;break;}
        vx_ip_address a={.kind=VX_IP_MULTICAST};rc=address(&a,p->ifma_addr,index);if(!rc)rc=add(s,&a);
    }freeifmaddrs(list);return rc;
}
static int same_topology(struct ifaddrs *a,struct ifaddrs *b){
    size_t na=0,nb=0;for(struct ifaddrs *p=b;p;p=p->ifa_next)if(p->ifa_addr)nb++;
    for(struct ifaddrs *p=a;p;p=p->ifa_next){if(!p->ifa_addr)continue;na++;int found=0;
        for(struct ifaddrs *q=b;q;q=q->ifa_next)if(q->ifa_addr&&!strcmp(p->ifa_name,q->ifa_name)&&p->ifa_flags==q->ifa_flags&&p->ifa_addr->sa_len==q->ifa_addr->sa_len&&!memcmp(p->ifa_addr,q->ifa_addr,p->ifa_addr->sa_len)){
            if((!p->ifa_netmask)!=(!q->ifa_netmask))continue;
            if(p->ifa_netmask&&(p->ifa_netmask->sa_len!=q->ifa_netmask->sa_len||memcmp(p->ifa_netmask,q->ifa_netmask,p->ifa_netmask->sa_len)))continue;
            if((!p->ifa_dstaddr)!=(!q->ifa_dstaddr))continue;
            if(p->ifa_dstaddr&&(p->ifa_dstaddr->sa_len!=q->ifa_dstaddr->sa_len||memcmp(p->ifa_dstaddr,q->ifa_dstaddr,p->ifa_dstaddr->sa_len)))continue;
            found=1;break;
        }if(!found)return 0;
    }return na==nb;
}
static int included(const vx_ip_address *a,uint32_t family,uint32_t flags){
    if(family&&a->family!=family)return 0;
    return (a->kind==VX_IP_UNICAST&&!(flags&1))||(a->kind==VX_IP_ANYCAST&&!(flags&2))||
           (a->kind==VX_IP_MULTICAST&&!(flags&4))||(a->kind==VX_IP_DNS&&!(flags&8))||(a->kind==VX_IP_GATEWAY&&(flags&0x80));
}
static int include_interface(const vx_ip_snapshot *s,size_t i,uint32_t family,uint32_t flags){
    if(flags&0x100)return 1;
    for(size_t j=0;j<s->address_count;j++)if(s->addresses[j].index==s->interfaces[i].index&&(!family||s->addresses[j].family==family)&&(s->addresses[j].kind==VX_IP_UNICAST||s->addresses[j].kind==VX_IP_ANYCAST))return 1;
    return 0;
}
typedef struct {uint8_t *buffer;size_t used;} arena;
static void *take(arena *a,size_t n){size_t p=a->used;a->used+=(n+7)&~(size_t)7;return a->buffer?a->buffer+p:NULL;}
static size_t wide_length(const uint16_t *s){size_t n=0;while(s[n])n++;return n;}
static void socket_out(void *p,const vx_ip_address *a,int prefix){
    uint8_t *b=p;uint16_t family=a->family;memcpy(b,&family,2);memcpy(b+(a->family==2?4:8),a->bytes,a->family==2?4:16);
    if(a->family==23)memcpy(b+24,&a->scope,4);
    if(prefix){uint8_t *ip=b+(a->family==2?4:8);int max=a->family==2?32:128;for(int bit=a->prefix;bit<max;bit++)ip[bit/8]&=~(0x80>>(bit%8));}
}
static void pack_pass(const vx_ip_snapshot *s,uint32_t family,uint32_t flags,arena *a){
    win_adapter *previous=NULL;
    for(size_t i=0;i<s->interface_count;i++){
        if(!include_interface(s,i,family,flags))continue;
        const vx_ip_interface *in=&s->interfaces[i];win_adapter *out=take(a,sizeof *out);
        size_t len=strlen(in->name)+1;char *name=take(a,len);uint16_t *description=take(a,len*2),*friendly=NULL,*suffix=take(a,(wide_length(in->suffix)+1)*2);
        if(!(flags&0x20))friendly=take(a,len*2);
        if(out){out->length=sizeof *out;out->index=in->index;out->index6=in->index;out->name=(uintptr_t)name;out->description=(uintptr_t)description;out->friendly=(uintptr_t)friendly;out->suffix=(uintptr_t)suffix;
            memcpy(name,in->name,len);for(size_t k=0;k<len;k++){description[k]=(uint8_t)name[k];if(friendly)friendly[k]=(uint8_t)name[k];}memcpy(suffix,in->suffix,(wide_length(in->suffix)+1)*2);
            memcpy(out->physical,in->physical,8);out->physical_length=in->physical_length;out->flags=in->flags;out->mtu=in->mtu;out->type=in->type;out->status=in->status;out->tx=out->rx=in->speed;out->zones[1]=out->zones[2]=in->index;
            if(previous)previous->next=(uintptr_t)out;previous=out;
        }
        win_node *tails[5]={0};win_prefix *prefix_tail=NULL;
        for(size_t j=0;j<s->address_count;j++){
            const vx_ip_address *ip=&s->addresses[j];if(ip->index!=in->index)continue;
            if(included(ip,family,flags)){
                size_t node_size=ip->kind==VX_IP_UNICAST?sizeof(win_unicast):sizeof(win_node);win_node *node=take(a,node_size);int salen=ip->family==2?16:28;void *sa=take(a,salen);
                if(node){node->length=node_size;node->address.pointer=(uintptr_t)sa;node->address.length=salen;socket_out(sa,ip,0);
                    if(tails[ip->kind])tails[ip->kind]->next=(uintptr_t)node;
                    else {uint64_t *heads[]={&out->unicast,&out->anycast,&out->multicast,&out->dns,&out->gateway};*heads[ip->kind]=(uintptr_t)node;}tails[ip->kind]=node;
                    if(ip->kind==VX_IP_UNICAST){win_unicast *u=(void*)node;u->prefix_origin=ip->prefix_origin;u->suffix_origin=ip->suffix_origin;u->dad=ip->dad;u->valid=ip->valid;u->preferred=ip->preferred;u->lease=ip->lease;u->prefix=ip->prefix;}
                }
            }
            if((flags&0x10)&&ip->kind==VX_IP_UNICAST&&(!family||family==ip->family)){
                win_prefix *node=take(a,sizeof *node);int salen=ip->family==2?16:28;void *sa=take(a,salen);
                if(node){node->node.length=sizeof *node;node->node.address.pointer=(uintptr_t)sa;node->node.address.length=salen;node->prefix=ip->prefix;socket_out(sa,ip,1);if(prefix_tail)prefix_tail->node.next=(uintptr_t)node;else out->prefix=(uintptr_t)node;prefix_tail=node;}
            }
        }
    }
}
uint32_t vx_ip_pack(const vx_ip_snapshot *s,uint32_t family,uint32_t flags,void *buffer,uint32_t *size){
    if(!size||(family!=0&&family!=2&&family!=23))return BAD_PARAM;
    if((flags&~SUPPORTED_FLAGS)||(flags&UNSUPPORTED_FLAGS))return UNSUPPORTED;
    if(s->interface_count>VX_IF_MAX||s->address_count>VX_IP_MAX)return BAD_DATA;
    for(size_t i=0;i<s->interface_count;i++)if(!memchr(s->interfaces[i].name,0,16)||s->interfaces[i].suffix[255]||s->interfaces[i].physical_length>8)return BAD_DATA;
    for(size_t j=0;j<s->address_count;j++){
        const vx_ip_address *ip=&s->addresses[j];if(ip->kind>VX_IP_GATEWAY||(ip->family!=2&&ip->family!=23)||ip->prefix>(ip->family==2?32:128))return BAD_DATA;
        if(included(ip,family,flags)&&ip->kind==VX_IP_UNICAST&&ip->source==VX_IP_UNKNOWN)return UNSUPPORTED;
    }
    arena count={0};pack_pass(s,family,flags,&count);if(!count.used)return NO_DATA;if(count.used>UINT32_MAX)return NOMEM;
    if(!buffer||*size<count.used){*size=(uint32_t)count.used;return BUFFER_OVERFLOW;}
    if((uintptr_t)buffer&7)return BAD_PARAM;
    memset(buffer,0,count.used);arena out={buffer,0};pack_pass(s,family,flags,&out);*size=(uint32_t)out.used;return OK;
}
uint32_t vx_get_adapters_addresses(uint32_t family,uint32_t flags,void *reserved,void *buffer,uint32_t *size){
    if(!size||reserved||(family!=0&&family!=2&&family!=23))return BAD_PARAM;
    if((flags&~SUPPORTED_FLAGS)||(flags&UNSUPPORTED_FLAGS))return UNSUPPORTED;
    int saved_errno=errno;uint32_t rc=NOMEM;vx_ip_snapshot *s=calloc(1,sizeof *s);if(!s){errno=saved_errno;return NOMEM;}
    for(int attempt=0;attempt<3;attempt++){
        memset(s,0,sizeof *s);struct ifaddrs *first=NULL,*last=NULL;
        if(getifaddrs(&first)){rc=UNSUPPORTED;break;}
        double now=CFAbsoluteTimeGetCurrent();rc=collect_addresses(s,first,(time_t)(now+kCFAbsoluteTimeIntervalSince1970));
        if(!rc)rc=collect_configuration(s,now,flags);
        if(!rc&&!(flags&4))rc=collect_multicast(s);
        if(!rc&&(flags&0x80))rc=collect_routes(s);
        if(!rc){if(getifaddrs(&last)||!same_topology(first,last))rc=RETRY;}
        freeifaddrs(first);if(last)freeifaddrs(last);
        if(rc!=RETRY)break;
    }
    if(!rc)rc=vx_ip_pack(s,family,flags,buffer,size);free(s);errno=saved_errno;return rc;
}
