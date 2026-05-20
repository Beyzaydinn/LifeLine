#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_in_init(void);
esp_err_t audio_in_start(void);
esp_err_t audio_in_stop(void);
esp_err_t audio_in_read_chunk(int16_t *out_samples, size_t max_samples, size_t *got_samples);
