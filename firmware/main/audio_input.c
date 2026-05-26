#include "audio_input.h"
#include "config.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_in";

static i2s_chan_handle_t s_rx_chan;
static bool s_running;

esp_err_t audio_in_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan), TAG, "new channel");

    /* INMP441 BCLK alt siniri 512 kHz'dir (datasheet). MONO modunda
     * 16 kHz × 32-bit × 1 = 512 kHz - tam sinir, mikrofon yari uyku
     * moduna giriyor ve veri yari hizda geliyor (test edildi: ses cok
     * yavas + bozuk duyuluyordu).
     *
     * STEREO modunda BCLK = 16 kHz × 32-bit × 2 = 1.024 MHz - guvenli
     * calisma araligi. L/R pini GND'ye bagli oldugu icin sadece sol
     * kanal anlamli veri verir; sag kanali read fonksiyonunda atiyoruz. */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_MIC_BCLK_GPIO,
            .ws = I2S_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_MIC_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_chan, &std_cfg), TAG, "init std");
    s_running = false;
    ESP_LOGI(TAG, "I2S mic init OK (%d Hz)", AUDIO_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t audio_in_start(void)
{
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "enable");
    s_running = true;
    return ESP_OK;
}

esp_err_t audio_in_stop(void)
{
    s_running = false;
    return i2s_channel_disable(s_rx_chan);
}

esp_err_t audio_in_read_chunk(int16_t *out_samples, size_t max_samples, size_t *got_samples)
{
    if (!s_running || !out_samples || !got_samples) {
        return ESP_ERR_INVALID_STATE;
    }

    /* STEREO modda her ses cercevesi 2 kanal x 32-bit = 8 byte. max_samples
     * mono cikis ornegi sayisi oldugu icin, ham buffer'da bunun 2 kati
     * 32-bit stereo ornegi okumamiz lazim.
     *
     * Sol kanal: raw[i*2]   - INMP441 anlamli veriyi burada veriyor
     * Sag kanal: raw[i*2+1] - L/R=GND'de sifir/gurultu, atiliyor
     *
     * Shift miktari: >> 11 ile yaklasik 8x gain. INMP441 -26 dBFS @ 94 dB
     * SPL hassasiyet ile normal konusma cok dusuk seviyede geliyor; gain
     * artirilmali ki Whisper kelimeleri tanibilsin. Clipping korumasi var. */
    int32_t raw[AUDIO_CHUNK_SAMPLES * 2];
    size_t bytes_read = 0;
    size_t want_bytes = max_samples * 2 * sizeof(int32_t);
    esp_err_t err = i2s_channel_read(s_rx_chan, raw, want_bytes, &bytes_read, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        return err;
    }

    size_t stereo_samples = bytes_read / sizeof(int32_t);
    size_t mono_samples = stereo_samples / 2;
    for (size_t i = 0; i < mono_samples; i++) {
        int32_t s = raw[i * 2] >> 11;
        if (s > 32767) {
            s = 32767;
        }
        if (s < -32768) {
            s = -32768;
        }
        out_samples[i] = (int16_t)s;
    }
    *got_samples = mono_samples;
    return ESP_OK;
}
