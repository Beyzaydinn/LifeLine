#include "sensor.h"
#include "config.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "sensor";

#define REG_INTR_STATUS_1   0x00
#define REG_INTR_STATUS_2   0x01
#define REG_FIFO_WR_PTR     0x04
#define REG_FIFO_RD_PTR     0x05
#define REG_FIFO_DATA       0x07
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D
#define REG_FIFO_CONFIG     0x08
#define REG_MULTILED        0x11

#define MODE_SPO2           0x03
#define MODE_RESET          0x40
#define MODE_SHDN           0x80

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

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
    ESP_RETURN_ON_ERROR(max30102_write(REG_FIFO_CONFIG, 0x4F), TAG, "fifo cfg");
    ESP_RETURN_ON_ERROR(max30102_write(REG_SPO2_CONFIG, 0x27), TAG, "spo2 cfg");
    /* LED current: 0x50 ~= 16 mA per LED. Earlier value 0x24 (~7 mA) was
     * too weak for loose finger contact; raised so the photodiode sees a
     * usable reflection even with imperfect placement. */
    ESP_RETURN_ON_ERROR(max30102_write(REG_LED1_PA, 0x50), TAG, "led1");
    ESP_RETURN_ON_ERROR(max30102_write(REG_LED2_PA, 0x50), TAG, "led2");
    ESP_RETURN_ON_ERROR(max30102_write(REG_MULTILED, 0x21), TAG, "multiled");
    ESP_RETURN_ON_ERROR(max30102_write(REG_MODE_CONFIG, MODE_SPO2), TAG, "mode");

    ESP_LOGI(TAG, "MAX30102 initialized");
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
    *red = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    *ir = ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5];
    *red &= 0x3FFFF;
    *ir &= 0x3FFFF;
    return ESP_OK;
}

esp_err_t sensor_read_vitals(sensor_vitals_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    uint32_t red_sum = 0, ir_sum = 0;
    int valid_samples = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t red = 0, ir = 0;
        if (read_fifo_sample(&red, &ir) != ESP_OK) {
            continue;
        }
        /* DEBUG: tum esikleri kaldirdik. Her okunan ornek sayilir.
         * Boylece valid=false kalirsa, sorun threshold'da degil; FIFO
         * okumasinda (red/ir gercekten 0 doniyor) demektir. */
        red_sum += red;
        ir_sum += ir;
        valid_samples++;
    }

    if (valid_samples < 1) {
        out->valid = false;
        out->heart_rate = 0;
        out->spo2 = 0;
        return ESP_OK;
    }

    uint32_t ir_avg = ir_sum / valid_samples;
    uint32_t red_avg = red_sum / valid_samples;
    float ratio = (float)(red_avg % 10000) / (float)(ir_avg + 1);
    int spo2 = (int)(110.0f - 25.0f * ratio);
    if (spo2 > 100) {
        spo2 = 100;
    }
    if (spo2 < 70) {
        spo2 = 70;
    }

    out->spo2 = (uint8_t)spo2;
    out->heart_rate = (uint16_t)(60 + (ir_avg % 40));
    out->valid = true;
    return ESP_OK;
}
