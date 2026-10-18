#ifndef VX_CPUDIAG_H
#define VX_CPUDIAG_H
void vx_cpudiag_enable(void);
void vx_cpudiag_start(void);
char *vx_cpudiag_snapshot(void);
void vx_cpudiag_finish(void);
#endif
