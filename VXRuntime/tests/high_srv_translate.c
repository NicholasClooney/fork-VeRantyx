// Host-side allocation invariants, in addition to high_srv.exe's GPU checks.
#include "dxbc.h"
#include <stdio.h>
#include <string.h>
#include "shaders/high_srv_vs.h"
#include "shaders/high_srv_ps.h"
#include "shaders/high_srv_cs.h"
#include "shaders/high_srv_gs.h"
#include "shaders/high_srv_budget_vs.h"
static int failures;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s FAILED\n", __LINE__, #c); failures++; } } while (0)
static void translated(const unsigned char *code, size_t size, int stage, const unsigned *slots, unsigned count) {
    dxbc_shader s;
    int rc = dxbc_translate(code, size, &s);
    CHECK(rc == 0);
    if (rc) { printf("translation: %s\n", s.error); dxbc_free(&s); return; }
    CHECK(s.type == stage);
    unsigned occupied = 0;
    for (unsigned i = 0; i < DXBC_SRV_SLOTS; i++) {
        int wanted = 0;
        for (unsigned j = 0; j < count; j++) if (slots[j] == i) wanted = 1;
        CHECK(!!(s.srv_buffers[i / 64] & (UINT64_C(1) << (i % 64))) == wanted);
        if (!wanted) { CHECK(s.srv_binding[i] == 0xff); continue; }
        unsigned b = s.srv_binding[i];
        CHECK(b < 30);
        if (b >= 30) continue;
        CHECK(!(occupied & (1u << b))); occupied |= 1u << b;
        CHECK(b < 14 || b >= 22);
        CHECK(!(s.cbuffers & (1u << b)));
        if (stage == DXBC_VERTEX) CHECK(b < 14);
        if (stage == DXBC_GEOMETRY) CHECK(b < 26);
    }
    if (stage == DXBC_VERTEX) { CHECK(s.srv_binding[33] == 2); CHECK(s.srv_binding[38] == 3); }
    if (stage == DXBC_COMPUTE) { CHECK(s.srv_binding[33] == 22); CHECK(s.srv_binding[64] == 23); CHECK(s.srv_binding[127] == 24); }
    dxbc_free(&s);
}
int main(void) {
    const unsigned vs[] = {33,38}, ps[] = {127}, cs[] = {33,64,127};
    translated(high_srv_vs,sizeof high_srv_vs,DXBC_VERTEX,vs,2);
    translated(high_srv_ps,sizeof high_srv_ps,DXBC_PIXEL,ps,1);
    translated(high_srv_cs,sizeof high_srv_cs,DXBC_COMPUTE,cs,3);
    translated(high_srv_gs,sizeof high_srv_gs,DXBC_GEOMETRY,ps,1);
    dxbc_shader s;
    int rc = dxbc_translate(high_srv_budget_vs,sizeof high_srv_budget_vs,&s);
    CHECK(rc != 0); CHECK(strstr(s.error, "Metal buffer binding budget exhausted") != NULL);
    printf("15-SRV vertex budget: rc=%d error=%s\n",rc,s.error);
    dxbc_free(&s);
    printf("high_srv_translate: %d failed checks -> %s\n", failures, failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
