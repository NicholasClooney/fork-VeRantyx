#ifndef VX_WAITDIAG_H
#define VX_WAITDIAG_H
#include <stdint.h>
/* Default-off, bounded metadata only. Object tokens identify addresses/handle values,
 * not object lifetimes; reuse is explicitly reported as a limitation. */
typedef struct { unsigned slot, depth; } vx_waitdiag_scope;
void vx_waitdiag_enable(void);
vx_waitdiag_scope vx_waitdiag_begin(const char *api, uintptr_t object, uint32_t timeout, uintptr_t caller);
void vx_waitdiag_end(vx_waitdiag_scope *scope);
void vx_waitdiag_phase(const char *phase);
void vx_waitdiag_result(vx_waitdiag_scope *scope, int64_t result);
void vx_waitdiag_present(void);
char *vx_waitdiag_snapshot(void);
#define VX_WAITDIAG_SCOPE(api, object, timeout) \
    vx_waitdiag_scope wd_scope __attribute__((cleanup(vx_waitdiag_end))) = \
        vx_waitdiag_begin(api, (uintptr_t)(object), (uint32_t)(timeout), (uintptr_t)__builtin_return_address(0))
#endif
