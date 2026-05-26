/*
 * UART based PC link (replaces native USB CDC).
 *
 * Background: ESP32-S3 native USB-OTG via TinyUSB CDC caused a Windows-side
 * "ERROR_BAD_COMMAND" on every PySerial ClearCommError IOCTL, making the
 * stream unusable. The on-board USB-UART bridge (CP210x / CH9102) is
 * rock-solid on Windows, so the binary protocol now travels through UART0
 * at 921 600 baud — fast enough for 16 kHz mono 16-bit audio (32 KB/s).
 *
 * API surface intentionally mirrors the old usb_cdc.* module so main.c
 * needs minimal changes.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Configure UART0 (default console pins GPIO 43/44 on ESP32-S3-DevKitM-1)
 * at UART_BAUD_RATE and install the driver. Must be called once at boot. */
esp_err_t uart_link_init(void);

/* UART has no DTR handshake like USB CDC: link is "connected" as soon as
 * the driver is up. Returns true after a successful uart_link_init(). */
bool uart_link_connected(void);

/* Blocking write of `len` bytes to UART0. Returns ESP_OK on success. */
esp_err_t uart_link_send(const uint8_t *data, size_t len);

/* Read up to `max_len` bytes from UART0 with a per-call timeout in ms.
 * Returns the number of bytes actually read (>=0). */
int uart_link_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms);
