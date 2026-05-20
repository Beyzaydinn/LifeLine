#include "usb_cdc.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static const char *TAG = "usb_cdc";

#define CDC_PORT TINYUSB_CDC_ACM_0

static bool s_connected;

static void cdc_line_state_changed(int itf, cdcacm_event_t *event)
{
    (void)itf;
    s_connected = event->line_state_changed_data.dtr;
    ESP_LOGI(TAG, "CDC %s", s_connected ? "connected" : "disconnected");
}

esp_err_t usb_cdc_init(void)
{
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "tinyusb install");

    const tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = CDC_PORT,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = cdc_line_state_changed,
        .callback_line_coding_changed = NULL,
    };
    ESP_RETURN_ON_ERROR(tusb_cdc_acm_init(&acm_cfg), TAG, "cdc acm init");

    s_connected = false;
    ESP_LOGI(TAG, "USB CDC initialized (waiting for PC on OTG USB)");
    return ESP_OK;
}

bool usb_cdc_connected(void)
{
    return s_connected;
}

esp_err_t usb_cdc_send(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* PC must open the OTG serial port (DTR) before we transmit */
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!tusb_cdc_acm_initialized(CDC_PORT)) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > 512) {
            chunk = 512;
        }
        size_t queued = tinyusb_cdcacm_write_queue(CDC_PORT, data + offset, chunk);
        if (queued == 0) {
            return ESP_ERR_TIMEOUT;
        }
        esp_err_t err = tinyusb_cdcacm_write_flush(CDC_PORT, pdMS_TO_TICKS(200));
        if (err == ESP_ERR_NOT_FINISHED) {
            /* non-blocking flush still in progress */
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "CDC flush: %s", esp_err_to_name(err));
            return err;
        }
        offset += chunk;
    }
    return ESP_OK;
}

int usb_cdc_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || max_len == 0) {
        return 0;
    }
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        size_t rx_size = 0;
        esp_err_t err = tinyusb_cdcacm_read(CDC_PORT, buf, max_len, &rx_size);
        if (err == ESP_OK && rx_size > 0) {
            return (int)rx_size;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    return 0;
}
