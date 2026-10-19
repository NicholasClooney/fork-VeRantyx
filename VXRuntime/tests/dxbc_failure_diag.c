// Synthetic DXBC declarations: no game shaders or Metal device needed.
// clang -arch x86_64 -Isrc tests/dxbc_failure_diag.c src/dxbc.c -o /tmp/dxbc_failure_diag
#include "dxbc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "shaders/sum_cs.h"
#include "shaders/bounds_cs.h"

static uint32_t blob[1024];
static size_t count;
static void begin(void) {
    memset(blob, 0, sizeof blob);
    memcpy(blob, "DXBC", 4); blob[5] = 1; blob[7] = 1; blob[8] = 36;
    memcpy(blob + 9, "SHEX", 4); blob[11] = (5u << 16) | 0x50; count = 13;
}
static void decl(unsigned op, unsigned type, unsigned slot, unsigned extra) {
    unsigned dims = type == 8 ? 2 : 1;
    unsigned tail = op == 88 || op == 156 || op == 158 || op == 162;
    blob[count++] = op | ((1 + 1 + dims + tail) << 24);
    blob[count++] = (type << 12) | (dims << 20);
    blob[count++] = slot;
    if (dims == 2 || tail) blob[count++] = extra;
}
static size_t finish(void) {
    blob[count++] = 62 | (1u << 24); // ret
    blob[6] = (uint32_t)count * 4; blob[10] = (uint32_t)(count - 11) * 4;
    blob[12] = (uint32_t)count - 11;
    return count * 4;
}
static dxbc_resource_diag scan(size_t size) {
    dxbc_resource_diag d; dxbc_resource_summary(blob, size, &d); return d;
}
static int has(dxbc_resource_diag d, int kind, unsigned slot) {
    return !!(d.slots[kind][slot / 64] & (1ull << (slot % 64)));
}
static void unchanged(size_t n, int expected) {
    uint32_t copy[1024]; memcpy(copy, blob, n);
    dxbc_shader before, after;
    int a = dxbc_translate(blob, n, &before);
    dxbc_resource_diag d = scan(n);
    int b = dxbc_translate(blob, n, &after);
    assert(a == expected && b == a && !strcmp(before.error, after.error));
    assert(!memcmp(copy, blob, n) && d.status == DXBC_DIAG_COMPLETE);
    if (!a) assert(!strcmp(before.msl, after.msl));
    dxbc_free(&before); dxbc_free(&after);
}
int main(void) {
    begin(); decl(162, 7, 33, 16); // The translator stops here; the diagnostic must keep scanning.
    decl(89, 8, 13, 4); decl(161, 7, 127, 0); decl(88, 7, 64, 0x5555);
    decl(157, 30, 0, 0); decl(158, 30, 6, 4); decl(156, 30, 7, 0x4444);
    size_t n = finish(); dxbc_resource_diag d = scan(n);
    assert(d.status == DXBC_DIAG_COMPLETE && d.stage == DXBC_COMPUTE);
    assert(has(d, DXBC_DIAG_STRUCTURED, 33) && has(d, DXBC_DIAG_CB, 13));
    assert(has(d, DXBC_DIAG_RAW, 127) && has(d, DXBC_DIAG_TYPED, 64));
    assert(has(d, DXBC_DIAG_UAV_RAW, 0) && has(d, DXBC_DIAG_UAV_STRUCTURED, 6) && has(d, DXBC_DIAG_UAV_TYPED, 7));
    unchanged(n, -1);
    for (size_t i = 0; i < n; i++) assert(scan(i).status == DXBC_DIAG_MALFORMED);
    unsigned char unaligned[sizeof blob + 1]; memcpy(unaligned + 1, blob, n);
    dxbc_resource_summary(unaligned + 1, n, &d); assert(d.status == DXBC_DIAG_COMPLETE);
    uint32_t saved = blob[8]; blob[8] = 0xfffffffcu; assert(scan(n).status == DXBC_DIAG_MALFORMED); blob[8] = saved;
    saved = blob[10]; blob[10] = 0xfffffffcu; assert(scan(n).status == DXBC_DIAG_MALFORMED); blob[10] = saved;
    blob[12]++; assert(scan(n).status == DXBC_DIAG_MALFORMED); blob[12]--;
    blob[count - 1] = 0; d = scan(n);
    assert(d.status == DXBC_DIAG_MALFORMED && has(d, DXBC_DIAG_STRUCTURED, 33));
    blob[count - 1] = 62 | (127u << 24); assert(scan(n).status == DXBC_DIAG_MALFORMED);
    begin(); decl(161, 7, 0, 0); n = finish(); unchanged(n, 0);
    blob[14] |= 2u << 22; assert(scan(n).status == DXBC_DIAG_UNSUPPORTED); // relative index
    begin(); decl(161, 7, 128, 0); n = finish(); assert(scan(n).status == DXBC_DIAG_UNSUPPORTED);
    begin(); decl(89, 8, 14, 1); n = finish(); assert(scan(n).status == DXBC_DIAG_UNSUPPORTED);
    begin(); decl(157, 30, 8, 0); n = finish(); assert(scan(n).status == DXBC_DIAG_UNSUPPORTED);
    begin(); decl(162, 7, 33, 16); n = finish(); blob[13] = 162 | (2u << 24);
    assert(scan(n).status == DXBC_DIAG_MALFORMED); // missing immediate index and stride
    begin(); n = finish(); blob[13] = 62 | (1u << 24) | 0x80000000u;
    assert(scan(n).status == DXBC_DIAG_MALFORMED); // unterminated opcode extension
    begin(); blob[count++] = 53; blob[count++] = 2; decl(161, 7, 33, 0); n = finish();
    assert(scan(n).status == DXBC_DIAG_COMPLETE); blob[14] = 0xffffffffu;
    assert(scan(n).status == DXBC_DIAG_MALFORMED);
    begin(); n = finish(); blob[11] |= 1; assert(scan(n).status == DXBC_DIAG_UNSUPPORTED);
    dxbc_resource_summary(NULL, 0, &d); assert(d.status == DXBC_DIAG_MALFORMED);
    dxbc_resource_summary(NULL, DXBC_DIAG_MAX_BYTES + 1, &d); assert(d.status == DXBC_DIAG_LIMIT);
    begin(); blob[7] = 65; blob[6] = 292; assert(scan(292).status == DXBC_DIAG_LIMIT);
    begin();
    for (unsigned i = 0; i < 128; i++) decl(161, 7, i, 0);
    decl(161, 7, 33, 0); // A repeated declaration does not inflate the unique-slot count.
    n = finish(); d = scan(n);
    assert(d.status == DXBC_DIAG_COMPLETE && d.slots[DXBC_DIAG_RAW][0] == UINT64_MAX && d.slots[DXBC_DIAG_RAW][1] == UINT64_MAX);
    dxbc_resource_summary(sum_cs, sizeof sum_cs, &d);
    assert(d.status == DXBC_DIAG_COMPLETE && has(d, DXBC_DIAG_CB, 0) && has(d, DXBC_DIAG_STRUCTURED, 0));
    dxbc_resource_summary(bounds_cs, sizeof bounds_cs, &d);
    assert(d.status == DXBC_DIAG_COMPLETE && has(d, DXBC_DIAG_TYPED, 3) && has(d, DXBC_DIAG_RAW, 1));
    // Deterministic malformed-input coverage for the bounded scanner (never feed these to the translator).
    uint32_t seed = 1;
    for (unsigned j = 0; j < 20000; j++) {
        begin(); decl(162, 7, 33, 16); n = finish();
        seed = seed * 1664525u + 1013904223u;
        blob[seed % count] ^= seed;
        d = scan(n); assert(d.status >= DXBC_DIAG_COMPLETE && d.status <= DXBC_DIAG_LIMIT);
    }
    puts("dxbc_failure_diag: ok");
    return 0;
}
