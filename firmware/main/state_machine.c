#include "state_machine.h"
#include <string.h>

static system_state_t s_state = STATE_IDLE;
static uint8_t s_error = 0;
static SemaphoreHandle_t s_mutex;

void fsm_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_state = STATE_IDLE;
    s_error = 0;
}

SemaphoreHandle_t fsm_mutex(void)
{
    return s_mutex;
}

system_state_t fsm_get_state(void)
{
    system_state_t st = STATE_IDLE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        st = s_state;
        xSemaphoreGive(s_mutex);
    }
    return st;
}

void fsm_set_state(system_state_t state)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_state = state;
        if (state != STATE_ERROR) {
            s_error = 0;
        }
        xSemaphoreGive(s_mutex);
    }
}

void fsm_set_error(uint8_t error_code)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_state = STATE_ERROR;
        s_error = error_code;
        xSemaphoreGive(s_mutex);
    }
}

uint8_t fsm_get_error(void)
{
    return s_error;
}
