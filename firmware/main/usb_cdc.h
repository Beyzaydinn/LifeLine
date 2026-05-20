#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t usb_cdc_init(void);
bool usb_cdc_connected(void);
esp_err_t usb_cdc_send(const uint8_t *data, size_t len);
int usb_cdc_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms);
