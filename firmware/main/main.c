#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "state_machine.h"
#include "protocol.h"
#include "crypto_aes.h"
#include "uart_link.h"
#include "audio_input.h"
#include "audio_output.h"
#include "sensor.h"
#include "ppg.h"
#include "led_strip_ctrl.h"

static const char *TAG = "main";

typedef struct {
    uint8_t buf[PROTO_MAX_PAYLOAD + 64];
    size_t len;
} frame_item_t;

static QueueHandle_t s_tx_queue;
static QueueHandle_t s_playback_queue;

static protocol_parser_t s_parser;
static bool s_crypto_announced;
static bool s_status_sent;

/* PLAYBACK_END robust sinyali: queue uzerinden gondersek kuyruk doluyken
 * drop oluyor (xQueueSend 0 timeout). Flag ile gonderince UART RX
 * task'inde her durumda guvenli set edilir, task_playback kuyruk drain
 * olduktan sonra okur. */
static volatile bool s_playback_end_requested;

/* DIAG: AUDIO_DOWN queue overflow sayaci. Calismayan bir test sonrasi
 * loglara bakinca kac chunk drop olduguni gormek icin. */
static uint32_t s_audio_dropped;

static esp_err_t queue_frame(uint8_t type, const uint8_t *payload, size_t payload_len, bool encrypt)
{
    uint8_t work[PROTO_MAX_PAYLOAD + 64];
    const uint8_t *send_payload = payload;
    size_t send_len = payload_len;

    if (encrypt && payload && payload_len > 0) {
        size_t enc_len = 0;
        if (crypto_encrypt_payload(payload, payload_len, work, sizeof(work), &enc_len) != ESP_OK) {
            return ESP_FAIL;
        }
        send_payload = work;
        send_len = enc_len;
    }

    uint8_t frame[PROTO_MAX_PAYLOAD + 32];
    size_t frame_len = 0;
    if (protocol_frame_pack(type, send_payload, send_len, frame, sizeof(frame), &frame_len) != ESP_OK) {
        return ESP_FAIL;
    }

    frame_item_t item;
    if (frame_len > sizeof(item.buf)) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(item.buf, frame, frame_len);
    item.len = frame_len;
    if (xQueueSend(s_tx_queue, &item, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void send_status(system_state_t st, uint8_t err)
{
    uint8_t pl[2] = {(uint8_t)st, err};
    queue_frame(MSG_STATUS, pl, 2, false);
}

static void send_sensor_reading(void)
{
    sensor_vitals_t v;
    if (sensor_read_vitals(&v) != ESP_OK) {
        return;
    }
    uint8_t pl[4] = {
        (uint8_t)((v.heart_rate >> 8) & 0xFF),
        (uint8_t)(v.heart_rate & 0xFF),
        v.spo2,
        v.valid ? 1 : 0,
    };
    queue_frame(MSG_SENSOR, pl, 4, true);
}

static void start_recording(void)
{
    system_state_t st = fsm_get_state();
    if (st != STATE_IDLE) {
        return;
    }
    fsm_set_state(STATE_RECORDING);
    audio_in_start();
    led_strip_set_pattern(STATE_RECORDING);
    send_status(STATE_RECORDING, 0);
    ESP_LOGI(TAG, "Recording started (PC command)");
}

static void stop_recording(void)
{
    if (fsm_get_state() != STATE_RECORDING) {
        return;
    }
    audio_in_stop();
    fsm_set_state(STATE_PROCESSING);
    led_strip_set_pattern(STATE_PROCESSING);
    send_status(STATE_PROCESSING, 0);
    ESP_LOGI(TAG, "Recording stopped (PC command)");
}

static void on_frame_received(uint8_t type, const uint8_t *payload, size_t len, void *ctx)
{
    (void)payload;
    (void)len;
    (void)ctx;

    switch (type) {
    case MSG_START_RECORD:
        start_recording();
        break;
    case MSG_STOP_RECORD:
        stop_recording();
        break;
    case MSG_READ_SENSOR:
        send_sensor_reading();
        break;
    case MSG_AUDIO_DOWN: {
        uint8_t plain[PROTO_MAX_PAYLOAD];
        size_t plain_len = 0;
        if (crypto_decrypt_payload(payload, len, plain, sizeof(plain), &plain_len) != ESP_OK) {
            fsm_set_error(0x02);
            return;
        }
        /* Eski mantik: "ne SPEAKING ne PROCESSING ise SPEAKING'e gec".
         * Sorun: stop_recording ESP32'yi PROCESSING'e koyuyor, ilk
         * AUDIO_DOWN geldiginde state PROCESSING -> kosul false -> SPEAKING'e
         * gecilmiyor, audio_out_start cagrilmiyor, ses calinmiyor.
         * Dogru mantik: "SPEAKING'te degilsek SPEAKING'e gec" (PROCESSING
         * dahil tum diger state'lerden buraya gecmek mantikli). */
        if (fsm_get_state() != STATE_SPEAKING) {
            fsm_set_state(STATE_SPEAKING);
            audio_out_start();
            led_strip_set_pattern(STATE_SPEAKING);
        }
        frame_item_t chunk;
        if (plain_len > sizeof(chunk.buf)) {
            plain_len = sizeof(chunk.buf);
        }
        memcpy(chunk.buf, plain, plain_len);
        chunk.len = plain_len;
        if (xQueueSend(s_playback_queue, &chunk, 0) != pdTRUE) {
            s_audio_dropped++;
            /* Her 50 chunk drop'unda bir warn bas: spam'sizdir, gercek
             * bir flow control sorununu ortaya cikarir. */
            if ((s_audio_dropped % 50) == 1) {
                ESP_LOGW(TAG, "AUDIO_DOWN dropped (total=%lu)",
                         (unsigned long)s_audio_dropped);
            }
        }
        break;
    }
    case MSG_PLAYBACK_END: {
        /* Flag-based: queue dolu olsa bile guvenle set edilir.
         * task_playback queue drain edip flag'i kontrol edecek. */
        s_playback_end_requested = true;
        break;
    }
    case MSG_RESET:
        fsm_set_state(STATE_IDLE);
        audio_in_stop();
        audio_out_stop();
        led_strip_set_pattern(STATE_IDLE);
        send_status(STATE_IDLE, 0);
        break;
    default:
        break;
    }
}

static void task_uart_tx(void *arg)
{
    (void)arg;
    frame_item_t item;
    while (1) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) == pdTRUE) {
            if (uart_link_send(item.buf, item.len) != ESP_OK) {
                /* Host not connected yet — drop frame safely */
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }
}

static void task_uart_rx(void *arg)
{
    (void)arg;
    uint8_t buf[256];
    while (1) {
        int n = uart_link_read(buf, sizeof(buf), 50);
        if (n > 0) {
            protocol_parser_feed(&s_parser, buf, (size_t)n, on_frame_received, NULL);
        }
    }
}

static void task_audio_in(void *arg)
{
    (void)arg;
    int16_t samples[AUDIO_CHUNK_SAMPLES];
    while (1) {
        if (fsm_get_state() == STATE_RECORDING) {
            size_t got = 0;
            if (audio_in_read_chunk(samples, AUDIO_CHUNK_SAMPLES, &got) == ESP_OK && got > 0) {
                size_t bytes = got * sizeof(int16_t);
                queue_frame(MSG_AUDIO_UP, (uint8_t *)samples, bytes, true);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void task_sensor(void *arg)
{
    (void)arg;
    while (1) {
        if (fsm_get_state() == STATE_RECORDING) {
            send_sensor_reading();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void task_playback(void *arg)
{
    (void)arg;
    frame_item_t item;
    while (1) {
        /* Queue'dan al; 100 ms icinde gelmezse flag'i kontrol et.
         * Bu, PLAYBACK_END flag'inin queue empty kalsa bile gorulmesini
         * saglar. portMAX_DELAY kullanilsaydi, queue boslandiktan sonra
         * task burada sonsuza dek beklerdi ve PLAYBACK_END asla
         * islenmezdi. */
        if (xQueueReceive(s_playback_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            size_t samples = item.len / sizeof(int16_t);
            audio_out_write((const int16_t *)item.buf, samples);
        } else if (s_playback_end_requested && fsm_get_state() == STATE_SPEAKING) {
            /* Queue empty (timeout), PLAYBACK_END istendi: hoparlor susturup
             * IDLE'a don. DMA'da hala buffered ses olabilir; audio_out_stop
             * i2s_channel_disable cagiriyor, kalanlar druschd olur. */
            s_playback_end_requested = false;
            audio_out_stop();
            fsm_set_state(STATE_IDLE);
            led_strip_set_pattern(STATE_IDLE);
            send_status(STATE_IDLE, 0);
            if (s_audio_dropped > 0) {
                ESP_LOGW(TAG, "Playback finished; %lu AUDIO_DOWN chunks dropped during session",
                         (unsigned long)s_audio_dropped);
                s_audio_dropped = 0;
            }
        }
    }
}

static void task_led(void *arg)
{
    (void)arg;
    while (1) {
        led_strip_task_tick();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void task_heartbeat(void *arg)
{
    (void)arg;
    /* Let USB enumerate before first transmit */
    vTaskDelay(pdMS_TO_TICKS(1500));
    while (1) {
        if (uart_link_connected()) {
            if (!s_status_sent) {
                send_status(STATE_IDLE, 0);
                s_status_sent = true;
            }
            if (!s_crypto_announced) {
                uint8_t cap[2] = {1, CRYPTO_FLAG_ENCRYPTED};
                queue_frame(MSG_CRYPTO_CAP, cap, 2, false);
                s_crypto_announced = true;
            }
            uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000ULL);
            uint8_t pl[4] = {
                (uint8_t)((up >> 24) & 0xFF),
                (uint8_t)((up >> 16) & 0xFF),
                (uint8_t)((up >> 8) & 0xFF),
                (uint8_t)(up & 0xFF),
            };
            queue_frame(MSG_HEARTBEAT, pl, 4, false);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void task_error_recovery(void *arg)
{
    (void)arg;
    while (1) {
        if (fsm_get_state() == STATE_ERROR) {
            vTaskDelay(pdMS_TO_TICKS(1500));
            fsm_set_state(STATE_IDLE);
            led_strip_set_pattern(STATE_IDLE);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "First Aid Assistant — ESP32-S3 (no external button)");
    ESP_LOGI(TAG, "Hardware: INMP441, MAX98357A, MAX30102, WS2812B x8");
    ESP_ERROR_CHECK(nvs_flash_init());

    fsm_init();
    protocol_parser_init(&s_parser);

    ESP_ERROR_CHECK(crypto_init());
    if (!crypto_self_test()) {
        ESP_LOGE(TAG, "Crypto self-test failed");
    }
    if (!ppg_self_test()) {
        ESP_LOGE(TAG, "PPG self-test failed");
    }

    ESP_ERROR_CHECK(uart_link_init());
    ESP_ERROR_CHECK(led_strip_init());
    ESP_ERROR_CHECK(sensor_init());
    ESP_ERROR_CHECK(audio_in_init());
    ESP_ERROR_CHECK(audio_out_init());

    s_tx_queue = xQueueCreate(PROTO_TX_QUEUE_LEN, sizeof(frame_item_t));
    s_playback_queue = xQueueCreate(PROTO_RX_QUEUE_LEN, sizeof(frame_item_t));

    xTaskCreate(task_uart_tx, "uart_tx", TASK_STACK_UART, NULL, TASK_PRIO_UART_TX, NULL);
    xTaskCreate(task_uart_rx, "uart_rx", TASK_STACK_UART, NULL, TASK_PRIO_UART_RX, NULL);
    xTaskCreate(task_audio_in, "audio_in", TASK_STACK_AUDIO, NULL, TASK_PRIO_AUDIO_IN, NULL);
    xTaskCreate(task_sensor, "sensor", TASK_STACK_DEFAULT, NULL, TASK_PRIO_SENSOR, NULL);
    xTaskCreate(task_playback, "playback", TASK_STACK_AUDIO, NULL, TASK_PRIO_AUDIO_OUT, NULL);
    xTaskCreate(task_led, "led", TASK_STACK_DEFAULT, NULL, TASK_PRIO_LED, NULL);
    xTaskCreate(task_heartbeat, "heartbeat", TASK_STACK_DEFAULT, NULL, TASK_PRIO_HEARTBEAT, NULL);
    xTaskCreate(task_error_recovery, "err_rec", 2048, NULL, 2, NULL);

    led_strip_set_pattern(STATE_IDLE);
    ESP_LOGI(TAG, "Ready — connect OTG USB + python gui.py, then open COM port");
}
