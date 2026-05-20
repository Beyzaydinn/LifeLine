#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t audio_out_init(void);
esp_err_t audio_out_set_mute(bool mute);
esp_err_t audio_out_start(void);
esp_err_t audio_out_stop(void);
esp_err_t audio_out_write(const int16_t *samples, size_t num_samples);
