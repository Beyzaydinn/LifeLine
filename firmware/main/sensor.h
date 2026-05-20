#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint16_t heart_rate;
    uint8_t spo2;
    bool valid;
} sensor_vitals_t;

esp_err_t sensor_init(void);
esp_err_t sensor_read_vitals(sensor_vitals_t *out);
