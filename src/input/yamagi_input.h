#pragma once

#include <stdbool.h>

/*
 * Drain queued ACAP input and translate it into Yamagi key and mouse input.
 *
 * This function is called from Yamagi's IN_Update() on the game thread.
 * Relative mouse deltas are accumulated in mouse_x and mouse_y only while mouse_active is true.
 */
void acap_yamagi_input_update(float *mouse_x, float *mouse_y, bool mouse_active);
