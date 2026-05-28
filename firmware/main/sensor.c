#include "sensor.h"
#include "config.h"
#include "ppg.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor";

#define REG_FIFO_WR_PTR     0x04
#define REG_OVF_COUNTER     0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_DATA       0x07
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D
#define REG_MULTILED        0x11

#define MODE_SPO2           0x03
#define MODE_RESET          0x40

/* Effective rate after hardware averaging: SPO2_SR 100 Hz / SMP_AVE 2. */
#define SAMPLE_RATE_HZ      50.0f
#define SAMPLE_TICK_MS      80          /* FIFO drain cadence */
#define ANALYZE_EVERY_TICKS 6           /* ~480 ms between analyses */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* Chronological ring buffer of raw samples. */
static int32_t s_ir[PPG_MAX_SAMPLES];
static int32_t s_red[PPG_MAX_SAMPLES];
static int s_widx;      /* next write index */
static int s_count;     /* filled count (saturates at PPG_MAX_SAMPLES) */

/* Linear copies handed to ppg_compute (chronological order). */
static int32_t s_lin_ir[PPG_MAX_SAMPLES];
static int32_t s_lin_red[PPG_MAX_SAMPLES];

/* Shared result published to readers. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static sensor_vitals_t s_vitals;   /* {heart_rate, spo2, valid} */

static esp_err_t max30102_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t max30102_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t max30102_reset(void)
{
    ESP_RETURN_ON_ERROR(max30102_write(REG_MODE_CONFIG, MODE_RESET), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t read_fifo_sample(uint32_t *red, uint32_t *ir)
{
    uint8_t reg = REG_FIFO_DATA;
    uint8_t data[6];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, data, 6, 100);
    if (err != ESP_OK) {
        return err;
    }
    /* SpO2 mode FIFO order: RED then IR, 18-bit each. */
    *red = (((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]) & 0x3FFFF;
    *ir  = (((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5]) & 0x3FFFF;
    return ESP_OK;
}

/* Drain every sample currently in the hardware FIFO into the ring buffer. */
static void drain_fifo(void)
{
    uint8_t wr = 0, rd = 0;
    if (max30102_read(REG_FIFO_WR_PTR, &wr) != ESP_OK) return;
    if (max30102_read(REG_FIFO_RD_PTR, &rd) != ESP_OK) return;
    int navail = ((int)wr - (int)rd) & 0x1F;     /* FIFO is 32 deep */
    for (int i = 0; i < navail; i++) {
        uint32_t red = 0, ir = 0;
        if (read_fifo_sample(&red, &ir) != ESP_OK) break;
        s_ir[s_widx] = (int32_t)ir;
        s_red[s_widx] = (int32_t)red;
        s_widx = (s_widx + 1) % PPG_MAX_SAMPLES;
        if (s_count < PPG_MAX_SAMPLES) s_count++;
    }
}

/* Copy the ring buffer into linear chronological arrays, run the DSP, and
 * publish the result. */
static void analyze_and_publish(void)
{
    int n = s_count;
    if (n < 1) return;
    int start = (s_widx - n + PPG_MAX_SAMPLES) % PPG_MAX_SAMPLES;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % PPG_MAX_SAMPLES;
        s_lin_ir[i] = s_ir[idx];
        s_lin_red[i] = s_red[idx];
    }
    ppg_result_t r;
    ppg_compute(s_lin_ir, s_lin_red, n, SAMPLE_RATE_HZ, &r);

    taskENTER_CRITICAL(&s_mux);
    s_vitals.heart_rate = r.heart_rate;
    s_vitals.spo2 = r.spo2;
    s_vitals.valid = r.valid;
    taskEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "vitals: hr=%u spo2=%u valid=%d (n=%d)",
             (unsigned)r.heart_rate, (unsigned)r.spo2, r.valid, n);
}

static void sensor_task(void *arg)
{
    (void)arg;
    int tick = 0;
    while (1) {
        drain_fifo();
        if (++tick >= ANALYZE_EVERY_TICKS) {
            tick = 0;
            analyze_and_publish();
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_TICK_MS));
    }
}

esp_err_t sensor_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "dev");

    ESP_RETURN_ON_ERROR(max30102_reset(), TAG, "reset chip");
    /* FIFO_CONFIG 0x30: sample averaging 2 (-> 50 Hz effective), rollover ON
     * (oldest overwritten if we ever fall behind, never freezes), almost-full
     * threshold unused (we poll). */
    ESP_RETURN_ON_ERROR(max30102_write(REG_FIFO_CONFIG, 0x30), TAG, "fifo cfg");
    /* SPO2_CONFIG 0x27: 4096 nA range, 100 Hz, 411 us / 18-bit. */
    ESP_RETURN_ON_ERROR(max30102_write(REG_SPO2_CONFIG, 0x27), TAG, "spo2 cfg");
    /* LED current ~16 mA: strong enough for loose finger contact. */
    ESP_RETURN_ON_ERROR(max30102_write(REG_LED1_PA, 0x50), TAG, "led1");
    ESP_RETURN_ON_ERROR(max30102_write(REG_LED2_PA, 0x50), TAG, "led2");
    ESP_RETURN_ON_ERROR(max30102_write(REG_MULTILED, 0x21), TAG, "multiled");
    ESP_RETURN_ON_ERROR(max30102_write(REG_MODE_CONFIG, MODE_SPO2), TAG, "mode");

    /* Start from a clean FIFO. */
    ESP_RETURN_ON_ERROR(max30102_write(REG_FIFO_WR_PTR, 0x00), TAG, "wr ptr");
    ESP_RETURN_ON_ERROR(max30102_write(REG_OVF_COUNTER, 0x00), TAG, "ovf");
    ESP_RETURN_ON_ERROR(max30102_write(REG_FIFO_RD_PTR, 0x00), TAG, "rd ptr");

    s_widx = 0;
    s_count = 0;
    s_vitals.heart_rate = 0;
    s_vitals.spo2 = 0;
    s_vitals.valid = false;

    BaseType_t ok = xTaskCreate(sensor_task, "ppg_sampler",
                                TASK_STACK_DEFAULT, NULL, TASK_PRIO_SENSOR, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "sensor task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MAX30102 initialized (continuous PPG @ %.0f Hz)", SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t sensor_read_vitals(sensor_vitals_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_mux);
    *out = s_vitals;
    taskEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}
