#ifndef FH2_LAUNCH_CONTROL_H
#define FH2_LAUNCH_CONTROL_H
#include "shared/extra_options.h"

extern PatchDefinition launch_control_hook_patches[2];
void launch_control_bind(unsigned char *image, const ModOptions *options);
// Before the local car's native update: holds the revs while the launch control binding (and
// the throttle) are held at a standstill, by setting the throttle the physics reads this frame.
void launch_control_tick(void *car, float dt, int focused);
#endif
