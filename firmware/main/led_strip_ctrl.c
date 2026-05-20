#include "led_strip_ctrl.h"
#include "config.h"

#include <math.h>
#include "esp_log.h"
#include "esp_check.h"
#include "led_strip.h"

static const char *TAG = "led";

static led_strip_handle_t s_strip;
static system_state_t s_anim_state = STATE_IDLE;
static uint32_t s_tick;

static void set_pixel_rgb(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t br = (uint8_t)((r * LED_DEFAULT_BRIGHTNESS) / 255);
    uint8_t bg = (uint8_t)((g * LED_DEFAULT_BRIGHTNESS) / 255);
    uint8_t bb = (uint8_t)((b * LED_DEFAULT_BRIGHTNESS) / 255);
    led_strip_set_pixel(s_strip, index, br, bg, bb);
}

static void refresh(void)
{
    led_strip_refresh(s_strip);
}

esp_err_t led_strip_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip), TAG, "new strip");
    led_strip_clear(s_strip);
    refresh();
    ESP_LOGI(TAG, "NeoPixel init (%d LEDs, GPIO %d)", LED_STRIP_COUNT, LED_STRIP_GPIO);
    return ESP_OK;
}

void led_strip_set_pattern(system_state_t state)
{
    s_anim_state = state;
}

void led_strip_task_tick(void)
{
    s_tick++;
    led_strip_clear(s_strip);

    switch (s_anim_state) {
    case STATE_IDLE: {
        float phase = (s_tick % 80) / 80.0f;
        uint8_t g = (uint8_t)(20 + 35 * (0.5f + 0.5f * sinf(phase * 6.28318f)));
        for (int i = 0; i < LED_STRIP_COUNT; i++) {
            set_pixel_rgb(i, 0, g, 0);
        }
        break;
    }
    case STATE_RECORDING: {
        int pos = (s_tick / 4) % LED_STRIP_COUNT;
        for (int i = 0; i < LED_STRIP_COUNT; i++) {
            if (i == pos) {
                set_pixel_rgb(i, 255, 0, 0);
            }
        }
        break;
    }
    case STATE_PROCESSING: {
        uint8_t p = (uint8_t)(40 + 40 * (0.5f + 0.5f * sinf((s_tick % 40) / 40.0f * 6.28318f)));
        for (int i = 0; i < LED_STRIP_COUNT; i++) {
            set_pixel_rgb(i, p, 0, p);
        }
        break;
    }
    case STATE_SPEAKING:
        for (int i = 0; i < LED_STRIP_COUNT; i++) {
            set_pixel_rgb(i, 0, 200, 0);
        }
        break;
    case STATE_ERROR: {
        int flash = (s_tick / 8) % 6;
        if (flash < 3) {
            for (int i = 0; i < LED_STRIP_COUNT; i++) {
                set_pixel_rgb(i, 255, 0, 0);
            }
        }
        break;
    }
    default:
        break;
    }
    refresh();
}
