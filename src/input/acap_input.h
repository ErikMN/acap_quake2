#pragma once

#include "input_queue.h"

#include <stdbool.h>

bool acap_input_init(void);
void acap_input_shutdown(void);
bool acap_input_next_event(struct acap_input_event *event);
