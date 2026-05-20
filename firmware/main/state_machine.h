#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef enum {
    STATE_IDLE = 0,
    STATE_RECORDING,
    STATE_PROCESSING,
    STATE_SPEAKING,
    STATE_ERROR,
} system_state_t;

void fsm_init(void);
system_state_t fsm_get_state(void);
void fsm_set_state(system_state_t state);
void fsm_set_error(uint8_t error_code);
uint8_t fsm_get_error(void);
SemaphoreHandle_t fsm_mutex(void);
