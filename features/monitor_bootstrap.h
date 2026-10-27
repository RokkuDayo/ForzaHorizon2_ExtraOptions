#ifndef FH2_MONITOR_BOOTSTRAP_H
#define FH2_MONITOR_BOOTSTRAP_H
#include "extra_options.h"
/* Performance monitor (monitor\fh2-monitor.dll, source in monitor\): FPS
   counter and fh2-perf.csv. It loads after the game's D3D12CreateDevice,
   outside loader initialization, and only when FH2_ExtraOptions.ini
   [Monitor] ShowFps > 0 or PerfLog=1 asks for it. */
void monitor_arm(unsigned char *verified_host, const wchar_t *directory);
int monitor_requested(const wchar_t *directory);
/* Writes the monitor lines noted before the mod log was started; later lines
   are logged at once. */
void monitor_log_pending(void);
#endif
