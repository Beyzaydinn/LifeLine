#include "audio_output.h"
#include "config.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_out";

static i2s_chan_handle_t s_tx_chan;
static bool s_running;

esp_err_t audio_out_init(void)
{
    gpio_config_t sd_cfg = {
        .pin_bit_mask = 1ULL << I2S_SPK_SD_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&sd_cfg);
    gpio_set_level(I2S_SPK_SD_GPIO, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "new channel");

    /* MAX98357A 16 kHz × 16-bit × MONO = 256 kHz BCLK ile kararsiz
     * calisabiliyor (cok dusuk BCLK). slot_bit_width'i 32-bit yapinca
     * BCLK 512 kHz olur, MAX98357A guvenli aralikta calisir. data 16-bit
     * kaldigi icin audio_out_write'i degistirmemize gerek yok -- driver
     * 16-bit sample'i 32-bit slot'a MSB-aligned yerlestiriyor. */
    i2s_std_config_t std_cfg = {
        /* 16 kHz: yuvarlak sayi, ESP32 PLL temiz turetir. Pipeline hizini
         * ayarlamak icin Piper'i PC tarafinda yavaslatiyoruz. */
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SPK_BCLK_GPIO,
            .ws = I2S_SPK_WS_GPIO,
            .dout = I2S_SPK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "init std");
    s_running = false;
    ESP_LOGI(TAG, "I2S speaker init OK");
    return ESP_OK;
}

esp_err_t audio_out_set_mute(bool mute)
{
    gpio_set_level(I2S_SPK_SD_GPIO, mute ? 0 : 1);
    return ESP_OK;
}

esp_err_t audio_out_start(void)
{
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "enable");
    audio_out_set_mute(false);
    s_running = true;
    return ESP_OK;
}

esp_err_t audio_out_stop(void)
{
    s_running = false;
    audio_out_set_mute(true);
    return i2s_channel_disable(s_tx_chan);
}

esp_err_t audio_out_write(const int16_t *samples, size_t num_samples)
{
    if (!s_running || !samples || num_samples == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes_written = 0;
    size_t bytes = num_samples * sizeof(int16_t);
    return i2s_channel_write(s_tx_chan, samples, bytes, &bytes_written, pdMS_TO_TICKS(500));
}
