#pragma once

#include "state_machine.h"
#include "esp_err.h"

esp_err_t led_strip_init(void);
void led_strip_set_pattern(system_state_t state);
void led_strip_task_tick(void);
