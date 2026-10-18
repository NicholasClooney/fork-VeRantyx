#ifndef VX_RENDERDIAG_H
#define VX_RENDERDIAG_H
#include <stdint.h>
enum { RD_DEVICE, RD_DEVICE_SWAPCHAIN, RD_SWAPCHAIN, RD_SWAPCHAIN_HWND, RD_PRESENT, RD_PRESENT1, RD_API_COUNT };
enum { RD_TEST, RD_NO_DRAWABLE, RD_COMMITTED, RD_D3D12_DELEGATE, RD_STAGE_COUNT };
enum { RD_WORK_MAIN, RD_WORK_PRE, RD_CALLBACK_ENTERED, RD_CALLBACK_COMPLETED, RD_ACTIVITY_COUNT };
void vx_renderdiag_activity(unsigned activity);
void vx_renderdiag_enable(void);
unsigned vx_renderdiag_begin(unsigned api);
void vx_renderdiag_end(unsigned api, unsigned ticket, int32_t hr);
void vx_renderdiag_stage(unsigned stage);
char *vx_renderdiag_snapshot(void);
#endif
