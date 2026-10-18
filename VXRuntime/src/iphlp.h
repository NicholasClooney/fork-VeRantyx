#ifndef VX_IPHLP_H
#define VX_IPHLP_H
#include <stdint.h>
#include <stddef.h>
#define VX_IP_MAX 4096
#define VX_IF_MAX 256
enum vx_ip_source { VX_IP_UNKNOWN, VX_IP_LOOPBACK, VX_IP_MANUAL, VX_IP_DHCP, VX_IP_V6, VX_IP_UTUN, VX_IP_UTUN_PENDING };
enum vx_ip_kind { VX_IP_UNICAST, VX_IP_ANYCAST, VX_IP_MULTICAST, VX_IP_DNS, VX_IP_GATEWAY };
typedef struct {
    uint32_t index, flags, mtu, type, status;
    uint64_t speed;
    char name[16];
    uint8_t physical[8], physical_length;
    uint16_t suffix[256];
} vx_ip_interface;
typedef struct {
    uint32_t index, scope, kind, source, dad, prefix_origin, suffix_origin;
    uint32_t valid, preferred, lease;
    uint8_t family, prefix, bytes[16];
} vx_ip_address;
typedef struct {
    size_t interface_count, address_count;
    vx_ip_interface interfaces[VX_IF_MAX];
    vx_ip_address addresses[VX_IP_MAX];
} vx_ip_snapshot;
uint32_t vx_ip_lifetime(double now, double start, double end, uint32_t *remaining, uint32_t *lease);
uint32_t vx_ip_routes(vx_ip_snapshot *, const void *, size_t);
uint32_t vx_ip_pack(const vx_ip_snapshot *, uint32_t, uint32_t, void *, uint32_t *);
uint32_t vx_get_adapters_addresses(uint32_t, uint32_t, void *, void *, uint32_t *);
#endif
