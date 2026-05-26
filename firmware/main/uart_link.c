#include "uart_link.h"
#include "config.h"

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "uart_link";

#define LINK_UART_PORT       UART_NUM_0
/* RX buffer 2048'den 8192'ye buyutuldu: PC AUDIO_DOWN streaming sirasinda
 * burst halinde veri yolluyor, task_uart_rx 50 ms'de bir okuyor; 2 KB
 * tampon yetmez (50 ms'de 50+ KB veri gelebiliyor), frame kaybi olur. */
#define LINK_UART_RX_BUF     8192
#define LINK_UART_TX_BUF     4096

/* ESP32-S3-DevKitM-1 default UART0 pinleri (silkscreen TX0 / RX0):
 *  TX = GPIO 43, RX = GPIO 44 -> bunlar CP210x/CH9102 USB-UART koprusune
 *  baglidir, Windows'ta COM12 olarak gorunur. Acik atama zorunlu cunku
 *  CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG durumunda ESP-IDF startup UART0'i
 *  console olarak kullanmadigi icin ROM'un yaptigi pin matrix baglantisini
 *  da bozabiliyor. UART_PIN_NO_CHANGE bu durumda pinleri unattached birakir
 *  ve TX fiziksel olarak hicbir GPIO'ya cikmaz. */
#define LINK_UART_TX_GPIO    GPIO_NUM_43
#define LINK_UART_RX_GPIO    GPIO_NUM_44

static bool s_initialized;

esp_err_t uart_link_init(void)
{
    /* TX/RX pins are left at their default mapping (GPIO 43/44 on
     * ESP32-S3-DevKitM-1) so the on-board CP210x/CH9102 USB-UART bridge
     * carries the binary protocol without any wiring change. */
    uart_config_t cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(LINK_UART_PORT, LINK_UART_RX_BUF,
                                        LINK_UART_TX_BUF, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_param_config(LINK_UART_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* Explicit pin attach: ROM ve bootloader UART0 default pinlerini (43/44)
     * configure etmis olsa da, CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG aktifken
     * console UART0'i kullanmadigi icin ESP-IDF startup'inda bu baglanti
     * korunmuyor. UART_PIN_NO_CHANGE bu durumda TX'i fiziksel olarak
     * herhangi bir GPIO'ya cikarmiyor; sonuc: PC tarafinda 0 byte gelir
     * (DIAG frame counts since connect: {}). Explicit GPIO_NUM_43/44 ile
     * baglantiyi yeniden saglariz. */
    err = uart_set_pin(LINK_UART_PORT,
                      LINK_UART_TX_GPIO, LINK_UART_RX_GPIO,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "uart_link_init OK: UART0 @ %d baud, TX=GPIO%d, RX=GPIO%d",
             (int)UART_BAUD_RATE, LINK_UART_TX_GPIO, LINK_UART_RX_GPIO);
    return ESP_OK;
}

bool uart_link_connected(void)
{
    return s_initialized;
}

esp_err_t uart_link_send(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    int written = uart_write_bytes(LINK_UART_PORT, (const char *)data, len);
    if (written < 0) {
        return ESP_FAIL;
    }
    /* Wait until the TX FIFO drains so we don't pile frames on top of each
     * other under heavy load (max 200 ms). */
    uart_wait_tx_done(LINK_UART_PORT, pdMS_TO_TICKS(200));
    return ESP_OK;
}

int uart_link_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || max_len == 0 || !s_initialized) {
        return 0;
    }
    int n = uart_read_bytes(LINK_UART_PORT, buf, max_len, pdMS_TO_TICKS(timeout_ms));
    return (n > 0) ? n : 0;
}
