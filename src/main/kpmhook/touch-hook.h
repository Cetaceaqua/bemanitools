#pragma once

#include <stdbool.h>

void kpm_touch_hook_init(void);

/* Post touch event from window messages.
 * screen: 0 = Station 0, 1 = Station 1.
 * type: 1 = Touch Down, 2 = Touch Move / Drag, 4 = Touch Up / Release.
 */
void kpm_touch_post_event(int screen, int client_x, int client_y, int client_w, int client_h, int type);
