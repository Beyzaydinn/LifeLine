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

/* int16 mono -> int32 stereo (L+R duplicated) staging buffer. I2S
 * audio_input.c'deki gibi STEREO 32-bit format kullanir (bkz. audio_out_init
 * yorumu) -- her int16 mono sample (S) iki int32 slot'a yerlestirilir:
 *   L = S << 16, R = S << 16 (MSB-aligned, 32-bit data)
 * MAX98357A (L+R)/2 = S mono cikis verir.
 * Boyut: PROTO_MAX_PAYLOAD/2 (max int16 sample sayisi) × 2 (stereo) = 1024
 * int32 = 4 KB static. audio_out_write tek task'ten cagrildigi icin race yok. */
static int32_t s_buf32[(PROTO_MAX_PAYLOAD / sizeof(int16_t)) * 2];

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

    /* I2S TX konfigürasyonu: audio_input.c ile SIMETRIK pattern.
     *
     * data=32, slot=32 (auto, default), STEREO, slot_mask=BOTH, ws_width=32.
     * BCLK = 16000 × 32 × 2 = 1024 kHz (audio_input ile ayni, MAX98357A
     * guvenli aralikta). MAX98357A LRCLK = 16 kHz, her cerceve = 2 slot
     * (L, R), her slot 32 bit. MAX98357A 32-bit data otomatik tespit eder
     * ve (L+R)/2 olarak mono cikis verir.
     *
     * Bu konfigurasyonun NEDEN: data=16/slot=32/mono override seti ESP-IDF
     * v5 driver'inda muglak: ws_width default'ta 16 kaliyor (data_bit_width
     * macro icinden gelir), slot 32 ama WS her 16 BCLK'da toggle ediyor.
     * MAX98357A bu durumda data formatini otomatik tespit edemiyor,
     * pitch/amplitude dejenerasyonu olusuyor (boğuk, kalın ses). STEREO
     * 32-bit ile audio_input ile birebir ayni karadan giderek belirsizligi
     * ortadan kaldiriyoruz. audio_out_write mono->stereo duplicate ediyor. */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
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
    /* Mono int16 -> stereo int32 MSB-aligned. Her int16 sample iki int32
     * slot'a kopyalanir (L+R), her int32'nin ust 16 bit'i orijinal int16,
     * alt 16 bit 0 (pad). MAX98357A (L+R)/2 mono karistirma ile S degerini
     * alir. Bkz. audio_out_init yorumu. */
    const size_t buf_cap_stereo = sizeof(s_buf32) / sizeof(s_buf32[0]);
    const size_t max_mono = buf_cap_stereo / 2;
    if (num_samples > max_mono) {
        num_samples = max_mono;
    }
    for (size_t i = 0; i < num_samples; i++) {
        int32_t v = ((int32_t)samples[i]) << 16;
        s_buf32[i * 2]     = v;   // L
        s_buf32[i * 2 + 1] = v;   // R
    }
    size_t bytes_written = 0;
    size_t bytes = num_samples * 2 * sizeof(int32_t);   // 2 slots/frame, 4 byte/slot
    return i2s_channel_write(s_tx_chan, s_buf32, bytes, &bytes_written, pdMS_TO_TICKS(500));
}
