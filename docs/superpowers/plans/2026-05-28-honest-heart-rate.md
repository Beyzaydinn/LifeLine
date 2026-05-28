# Honest Heart-Rate (PPG) Measurement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fake modulo BPM/SpO2 with a real PPG pipeline computed on the ESP32: continuous MAX30102 sampling, beat detection for heart rate, ratio-of-ratios SpO2 as a labelled estimate; PC runs a windowed measurement with live progress.

**Architecture:** A pure, hardware-independent DSP module (`ppg.c`) does the math and is validated on-device by a boot-time self-test against synthetic known-BPM data (mirroring the existing `crypto_self_test()`). `sensor.c` owns the MAX30102: it runs a continuous sampler task that drains the FIFO into a ring buffer, calls `ppg_compute()` periodically, and publishes the latest vitals under a spinlock. The existing 4-byte `MSG_SENSOR` protocol is unchanged. On the PC, `serial_bridge.measure_vitals()` polls for ~12–15 s with a pure, unit-tested stability helper; `gui.py` shows progress and a final value with an `(estimate)` SpO2 label.

**Tech Stack:** ESP-IDF v5.5.3 (C, FreeRTOS), MAX30102 over I2C, Python 3 + Tkinter + pyserial on the PC.

---

## Testing strategy (read first)

This is embedded firmware + a hardware-bound GUI; there is no pytest harness in this repo (the established pattern is boot-time self-tests like `crypto_self_test()` and standalone `pc/_test_*.py` smoke scripts). The plan adapts TDD to that reality:

- **C DSP (`ppg.c`)** — tested by `ppg_self_test()` run at boot with synthetic ground-truth data, observed on the **LOG plane** (USB-Serial-JTAG, e.g. COM13). "Run the test" = flash + monitor.
- **PC stability logic** — the pure `vitals_stable()` helper is unit-tested by a standalone script `pc/_test_measure_settle.py` (no hardware needed).
- **Hardware integration** — manual verification: place fingertip, hand-count pulse for 30 s ×2, require agreement within ±5 BPM; no finger → `valid=false`.

Commands (run from an ESP-IDF v5.5.3-enabled terminal, in `firmware/`; substitute your own DATA/LOG COM ports — DATA is the CP210x/CH9102 bridge, LOG is the USB-JTAG port, per `CLAUDE.md`):

```
idf.py build flash            # build + flash over the DATA port
idf.py -p COM13 monitor       # watch LOG plane (Ctrl-] to exit)
```

PC test command (from `pc/`):

```
.\.venv\Scripts\python.exe _test_measure_settle.py
```

---

## File Structure

- **Create** `firmware/main/ppg.h` — pure PPG DSP interface: `ppg_result_t`, `ppg_compute()`, `ppg_self_test()`, `PPG_MAX_SAMPLES`.
- **Create** `firmware/main/ppg.c` — pure DSP: DC-block, smoothing, peak detection, IBI→BPM, ratio-of-ratios SpO2, plus the synthetic self-test.
- **Modify** `firmware/main/CMakeLists.txt` — add `ppg.c` to `SRCS`.
- **Modify** `firmware/main/main.c` — call `ppg_self_test()` at boot next to `crypto_self_test()`.
- **Modify** `firmware/main/sensor.c` — register change (avg 4→2, rollover ON), clear FIFO pointers, continuous sampler task + ring buffer + spinlock-protected shared vitals; `sensor_read_vitals()` becomes a cheap copy (no I2C).
- **Modify** `firmware/main/sensor.h` — declare `sensor_start()` if needed (the sampler task is created inside `sensor_init`, so no public change expected; left as-is unless a forward decl is required).
- **Modify** `pc/serial_bridge.py` — add pure `vitals_stable()` + `measure_vitals()`.
- **Create** `pc/_test_measure_settle.py` — standalone unit test for `vitals_stable()`.
- **Modify** `pc/gui.py` — "Read vitals" runs the windowed measurement with a progress bar, live text, `(estimate)` label; `_connect` no longer auto-measures.

---

## Task 1: Pure PPG DSP core + boot-time self-test

**Files:**
- Create: `firmware/main/ppg.h`
- Create: `firmware/main/ppg.c`
- Modify: `firmware/main/CMakeLists.txt:2-11` (SRCS list)
- Modify: `firmware/main/main.c:337-340` (after `crypto_self_test()`)

- [ ] **Step 1: Write the interface `ppg.h`**

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Maximum analysis window in samples. Sized for ~12 s at 50 Hz.
 * The sensor ring buffer and ppg.c scratch buffers both use this. */
#define PPG_MAX_SAMPLES 600

typedef struct {
    uint16_t heart_rate;  /* BPM; 0 when invalid */
    uint8_t  spo2;        /* % estimate; 0 when invalid */
    bool     valid;       /* true only with a finger + a confident beat train */
} ppg_result_t;

/* Compute heart rate and an SpO2 estimate from a window of raw MAX30102
 * samples. ir/red are raw 18-bit counts in chronological order; n samples
 * (clamped to PPG_MAX_SAMPLES); fs is the effective sample rate in Hz.
 * NOT reentrant: uses static scratch buffers, call from one thread only. */
void ppg_compute(const int32_t *ir, const int32_t *red, int n, float fs,
                 ppg_result_t *out);

/* Boot-time self-test with synthetic data (mirrors crypto_self_test()).
 * Returns true if a synthetic 72 BPM signal is recovered within ±5 BPM and
 * a flat (no-finger) signal is correctly rejected. Logs the outcome. */
bool ppg_self_test(void);
```

- [ ] **Step 2: Write `ppg.c` with the self-test and a STUB `ppg_compute` (the failing test)**

Write the real `ppg_self_test()` now, but stub `ppg_compute` so the test fails first.

```c
#include "ppg.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ppg";

void ppg_compute(const int32_t *ir, const int32_t *red, int n, float fs,
                 ppg_result_t *out)
{
    (void)ir; (void)red; (void)n; (void)fs;
    /* STUB: not implemented yet — self-test must fail until Step 5. */
    out->heart_rate = 0;
    out->spo2 = 0;
    out->valid = false;
}

bool ppg_self_test(void)
{
    static int32_t ir[500];
    static int32_t red[500];
    const float fs = 50.0f;
    const float f = 1.2f;            /* 1.2 Hz = 72 BPM */

    for (int i = 0; i < 500; i++) {
        float ph = 2.0f * 3.14159265f * f * (float)i / fs;
        ir[i]  = (int32_t)(100000.0f + 1500.0f * sinf(ph));
        red[i] = (int32_t)( 95000.0f + 1200.0f * sinf(ph));
    }
    ppg_result_t r;
    ppg_compute(ir, red, 500, fs, &r);
    bool hr_ok = r.valid && r.heart_rate >= 67 && r.heart_rate <= 77;

    for (int i = 0; i < 500; i++) { ir[i] = 1000; red[i] = 1000; }
    ppg_result_t r2;
    ppg_compute(ir, red, 500, fs, &r2);
    bool flat_ok = !r2.valid;

    ESP_LOGI(TAG,
             "PPG self-test: 72BPM-> hr=%u valid=%d [%s], flat-> valid=%d [%s]",
             r.heart_rate, r.valid, hr_ok ? "OK" : "FAIL",
             r2.valid, flat_ok ? "OK" : "FAIL");
    return hr_ok && flat_ok;
}
```

- [ ] **Step 3: Add `ppg.c` to the build and call the self-test at boot**

In `firmware/main/CMakeLists.txt`, add `"ppg.c"` to the `SRCS` list (after `"sensor.c"`):

```cmake
        "sensor.c"
        "ppg.c"
        "led_strip_ctrl.c"
```

In `firmware/main/main.c`, add the include near the other module includes (after `#include "sensor.h"`):

```c
#include "ppg.h"
```

Then in `app_main`, right after the crypto self-test block (currently lines 337-340):

```c
    if (!crypto_self_test()) {
        ESP_LOGE(TAG, "Crypto self-test failed");
    }
    if (!ppg_self_test()) {
        ESP_LOGE(TAG, "PPG self-test failed");
    }
```

- [ ] **Step 4: Build, flash, monitor — verify the self-test FAILS**

Run (ESP-IDF terminal, in `firmware/`):

```
idf.py build flash
idf.py -p COM13 monitor
```

Expected on the LOG plane at boot:

```
ppg: PPG self-test: 72BPM-> hr=0 valid=0 [FAIL], flat-> valid=1 [...]
main: PPG self-test failed
```

(The stub returns invalid, so `hr_ok` is FAIL. This confirms the test exercises `ppg_compute`.)

- [ ] **Step 5: Implement the real `ppg_compute`**

Replace the STUB `ppg_compute` in `ppg.c` with the full implementation. Keep the includes from Step 2.

```c
/* ---- tunables ---------------------------------------------------------- */
#define FINGER_IR_THRESHOLD   50000.0f   /* raw IR DC below this => no finger */
#define HP_ALPHA              0.995f     /* one-pole DC blocker pole */
#define PEAK_THRESH_FRAC      0.40f      /* fraction of AC peak used as gate */
#define MIN_IBI_MS            300.0f     /* 200 BPM ceiling */
#define MAX_IBI_MS            2000.0f    /* 30 BPM floor */
#define MIN_BEATS             4          /* accepted IBIs needed for a reading */
#define MAX_CV                0.25f      /* max IBI coefficient of variation */
#define MAX_PEAKS             64

/* Static scratch (single-threaded use; see header note). */
static float s_ac_ir[PPG_MAX_SAMPLES];
static float s_ac_red[PPG_MAX_SAMPLES];
static float s_smooth[PPG_MAX_SAMPLES];

static float mean_i32(const int32_t *x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) s += (double)x[i];
    return (float)(s / (double)n);
}

/* One-pole DC blocker: y[i] = x[i] - x[i-1] + a*y[i-1]. Removes baseline,
 * keeps the pulsatile AC component. */
static void hp_filter(const int32_t *x, int n, float *out)
{
    float prev_x = (float)x[0];
    float prev_y = 0.0f;
    out[0] = 0.0f;
    for (int i = 1; i < n; i++) {
        float xi = (float)x[i];
        float yi = xi - prev_x + HP_ALPHA * prev_y;
        out[i] = yi;
        prev_x = xi;
        prev_y = yi;
    }
}

/* 5-tap moving average, src -> dst (dst may not alias src). */
static void smooth5(const float *src, int n, float *dst)
{
    for (int i = 0; i < n; i++) {
        float sum = 0.0f;
        int cnt = 0;
        for (int k = i - 2; k <= i + 2; k++) {
            if (k >= 0 && k < n) { sum += src[k]; cnt++; }
        }
        dst[i] = sum / (float)cnt;
    }
}

static float amplitude(const float *a, int n)
{
    float mn = a[0], mx = a[0];
    for (int i = 1; i < n; i++) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    return mx - mn;
}

static void sort_floats(float *a, int n)   /* insertion sort, small n */
{
    for (int i = 1; i < n; i++) {
        float key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) { a[j + 1] = a[j]; j--; }
        a[j + 1] = key;
    }
}

void ppg_compute(const int32_t *ir, const int32_t *red, int n, float fs,
                 ppg_result_t *out)
{
    out->heart_rate = 0;
    out->spo2 = 0;
    out->valid = false;
    if (n > PPG_MAX_SAMPLES) n = PPG_MAX_SAMPLES;
    if (n < (int)(2.0f * fs)) return;          /* need >= ~2 s of data */

    /* 1) finger detection */
    float ir_dc = mean_i32(ir, n);
    if (ir_dc < FINGER_IR_THRESHOLD) return;

    /* 2) AC components */
    hp_filter(ir, n, s_ac_ir);
    hp_filter(red, n, s_ac_red);
    smooth5(s_ac_ir, n, s_smooth);

    /* 3) adaptive-threshold peak detection on the smoothed IR AC */
    float mx = s_smooth[0];
    for (int i = 1; i < n; i++) if (s_smooth[i] > mx) mx = s_smooth[i];
    float thr = PEAK_THRESH_FRAC * mx;
    int refractory = (int)(0.3f * fs);
    if (refractory < 1) refractory = 1;

    int peak_idx[MAX_PEAKS];
    int npeaks = 0;
    int last_peak = -refractory - 1;
    for (int i = 1; i < n - 1; i++) {
        if (s_smooth[i] > thr &&
            s_smooth[i] >= s_smooth[i - 1] &&
            s_smooth[i] > s_smooth[i + 1] &&
            (i - last_peak) >= refractory) {
            if (npeaks < MAX_PEAKS) peak_idx[npeaks++] = i;
            last_peak = i;
        }
    }

    /* 4) inter-beat intervals -> BPM */
    float ibi[MAX_PEAKS];
    int nibi = 0;
    float ms_per_sample = 1000.0f / fs;
    for (int k = 1; k < npeaks; k++) {
        float ms = (float)(peak_idx[k] - peak_idx[k - 1]) * ms_per_sample;
        if (ms >= MIN_IBI_MS && ms <= MAX_IBI_MS) ibi[nibi++] = ms;
    }
    if (nibi < MIN_BEATS) return;

    float mean_ibi = 0.0f;
    for (int i = 0; i < nibi; i++) mean_ibi += ibi[i];
    mean_ibi /= (float)nibi;
    float var = 0.0f;
    for (int i = 0; i < nibi; i++) {
        float d = ibi[i] - mean_ibi;
        var += d * d;
    }
    float cv = sqrtf(var / (float)nibi) / mean_ibi;
    if (cv > MAX_CV) return;                    /* irregular -> not confident */

    sort_floats(ibi, nibi);
    float median = (nibi & 1) ? ibi[nibi / 2]
                              : 0.5f * (ibi[nibi / 2 - 1] + ibi[nibi / 2]);
    int bpm = (int)(60000.0f / median + 0.5f);
    if (bpm < 30) bpm = 30;
    if (bpm > 200) bpm = 200;

    /* 5) SpO2 estimate (ratio-of-ratios) */
    int spo2 = 0;
    float red_dc = mean_i32(red, n);
    float ac_ir = amplitude(s_ac_ir, n);
    float ac_red = amplitude(s_ac_red, n);
    if (ir_dc > 0.0f && red_dc > 0.0f && ac_ir > 0.0f) {
        float ratio = (ac_red / red_dc) / (ac_ir / ir_dc);
        float spo2f = 104.0f - 17.0f * ratio;
        if (spo2f > 100.0f) spo2f = 100.0f;
        if (spo2f < 70.0f) spo2f = 70.0f;
        spo2 = (int)(spo2f + 0.5f);
    }

    out->heart_rate = (uint16_t)bpm;
    out->spo2 = (uint8_t)spo2;
    out->valid = true;
}
```

- [ ] **Step 6: Build, flash, monitor — verify the self-test PASSES**

Run:

```
idf.py build flash
idf.py -p COM13 monitor
```

Expected at boot (no "PPG self-test failed" line):

```
ppg: PPG self-test: 72BPM-> hr=72 valid=1 [OK], flat-> valid=0 [OK]
```

(hr within 67–77 and flat rejected. Exact hr may read 71–73.)

- [ ] **Step 7: Commit**

```bash
git add firmware/main/ppg.h firmware/main/ppg.c firmware/main/CMakeLists.txt firmware/main/main.c
git commit -m "Add pure PPG DSP core with boot-time self-test"
```

---

## Task 2: Continuous sampler + real vitals in `sensor.c`

**Files:**
- Modify: `firmware/main/sensor.c` (full rewrite of the read path; keep init structure)

This task is verified on hardware (LOG plane + manual pulse count), per the testing strategy.

- [ ] **Step 1: Rewrite `sensor.c`**

Replace the whole file with the following. It keeps the register setup (changing only `FIFO_CONFIG` and adding a FIFO-pointer clear), adds a ring buffer + spinlock-protected shared vitals, creates a continuous sampler task in `sensor_init`, and turns `sensor_read_vitals` into a cheap copy.

**Note — register map fix:** the old file defined `REG_FIFO_RD_PTR 0x05`, but per the MAX30102 datasheet `0x05` is `OVF_COUNTER` and the FIFO read pointer is `0x06`. The old code never read the read pointer (it burst-read `FIFO_DATA` blindly), so the wrong address was dormant. `drain_fifo()` reads the read pointer, so the corrected map (`OVF_COUNTER 0x05`, `FIFO_RD_PTR 0x06`) below is required.

```c
#include "sensor.h"
#include "config.h"
#include "ppg.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor";

#define REG_INTR_STATUS_1   0x00
#define REG_INTR_STATUS_2   0x01
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
             r.heart_rate, r.spo2, r.valid, n);
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
```

- [ ] **Step 2: Build + flash**

Run (in `firmware/`):

```
idf.py build flash
```

Expected: build succeeds; `ppg.c` and `sensor.c` compile with no warnings about the changed symbols.

- [ ] **Step 3: Monitor the LOG plane — no-finger then finger**

Run:

```
idf.py -p COM13 monitor
```

With **no finger** on the sensor, expect repeating lines (~2/s) with `valid=0`:

```
sensor: vitals: hr=0 spo2=0 valid=0 (n=...)
```

Place a fingertip gently and hold still. Within a few seconds expect `valid=1` and a plausible BPM (≈50–100 at rest):

```
sensor: vitals: hr=78 spo2=97 valid=1 (n=600)
```

- [ ] **Step 4: Manual-count acceptance (±5 BPM)**

Keep the fingertip still ~15 s and read the stabilised `hr`. Hand-count your pulse for 30 s and double it; repeat once. The ESP `hr` must agree within **±5 BPM** and be repeatable. Remove the finger → `valid=0` within ~1–2 s. If BPM is consistently off, tune `PEAK_THRESH_FRAC` / `FINGER_IR_THRESHOLD` in `ppg.c` and re-flash.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/sensor.c
git commit -m "Compute real BPM/SpO2 from continuous MAX30102 sampling"
```

---

## Task 3: PC windowed measurement + unit-tested stability helper

**Files:**
- Modify: `pc/serial_bridge.py` (add `vitals_stable()` near the top-level helpers, and `measure_vitals()` on `SerialBridge`)
- Create: `pc/_test_measure_settle.py`

- [ ] **Step 1: Write the failing test `pc/_test_measure_settle.py`**

```python
"""Unit test for serial_bridge.vitals_stable (no hardware needed)."""

from serial_bridge import vitals_stable


def test_too_few_readings_not_stable():
    assert vitals_stable([72, 72], tolerance=3, need=4) is False


def test_stable_when_last_n_within_tolerance():
    # last 4 within +/-3 -> stable
    assert vitals_stable([120, 60, 75, 76, 77, 78], tolerance=3, need=4) is True


def test_not_stable_when_spread_too_wide():
    assert vitals_stable([70, 90, 71, 95], tolerance=3, need=4) is False


def test_uses_only_the_last_n():
    # early jitter ignored once the tail settles
    assert vitals_stable([10, 200, 80, 81, 82, 83], tolerance=3, need=4) is True


if __name__ == "__main__":
    test_too_few_readings_not_stable()
    test_stable_when_last_n_within_tolerance()
    test_not_stable_when_spread_too_wide()
    test_uses_only_the_last_n()
    print("ALL TESTS PASSED")
```

- [ ] **Step 2: Run it to verify it fails**

Run (in `pc/`):

```
.\.venv\Scripts\python.exe _test_measure_settle.py
```

Expected: `ImportError: cannot import name 'vitals_stable' from 'serial_bridge'`.

- [ ] **Step 3: Implement `vitals_stable` + `measure_vitals` in `pc/serial_bridge.py`**

Add the pure helper at module level (e.g. just after the `Vitals` dataclass, before `class SerialBridge`):

```python
def vitals_stable(recent_hrs: list[int], tolerance: int = 3, need: int = 4) -> bool:
    """True once we have >= `need` readings whose last `need` heart rates all
    fall within `tolerance` BPM of each other (i.e. the reading has settled)."""
    if len(recent_hrs) < need:
        return False
    window = recent_hrs[-need:]
    return max(window) - min(window) <= tolerance
```

Add the method on `SerialBridge` (e.g. right after `read_sensor`):

```python
    def measure_vitals(
        self,
        duration_s: float = 15.0,
        settle_s: float = 2.0,
        poll_interval: float = 0.4,
        on_update=None,
    ) -> Vitals:
        """Poll the ESP for up to `duration_s`, returning early once readings
        are valid and stable for `settle_s`. Calls on_update(vitals, elapsed,
        duration_s) each poll for live GUI feedback. Returns the last reading
        (which may be invalid if no reliable signal was obtained)."""
        start = time.monotonic()
        recent_hrs: list[int] = []
        last = Vitals()
        stable_since: float | None = None
        while time.monotonic() - start < duration_s:
            loop_t0 = time.monotonic()
            v = self.read_sensor()  # sends READ_SENSOR, waits ~0.3 s, returns latest
            last = v
            elapsed = time.monotonic() - start
            if on_update:
                on_update(v, elapsed, duration_s)
            if v.valid and v.heart_rate > 0:
                recent_hrs.append(v.heart_rate)
                if vitals_stable(recent_hrs):
                    if stable_since is None:
                        stable_since = time.monotonic()
                    elif time.monotonic() - stable_since >= settle_s:
                        return v
                else:
                    stable_since = None
            else:
                recent_hrs.clear()
                stable_since = None
            # read_sensor already slept ~0.3 s; pad out to poll_interval
            rest = poll_interval - (time.monotonic() - loop_t0)
            if rest > 0:
                time.sleep(rest)
        return last
```

- [ ] **Step 4: Run the test to verify it passes**

Run (in `pc/`):

```
.\.venv\Scripts\python.exe _test_measure_settle.py
```

Expected: `ALL TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add pc/serial_bridge.py pc/_test_measure_settle.py
git commit -m "Add windowed measure_vitals with unit-tested stability helper"
```

---

## Task 4: GUI windowed measurement flow

**Files:**
- Modify: `pc/gui.py` (vitals frame: add progress bar + keep button handle; replace `_read_vitals`; adjust `_connect`)

This task is verified by running the GUI against the ESP (manual).

- [ ] **Step 1: Add a progress bar and a button handle to the vitals frame**

In `pc/gui.py`, replace the current vitals-frame block (lines 66-70):

```python
        vitals_fr = ttk.LabelFrame(self, text="MAX30102 — place finger on sensor", padding=8)
        vitals_fr.pack(fill=tk.X, padx=8, pady=4)
        self.vitals_var = tk.StringVar(value="HR: —   SpO2: —")
        ttk.Label(vitals_fr, textvariable=self.vitals_var).pack(side=tk.LEFT)
        ttk.Button(vitals_fr, text="Read vitals", command=self._read_vitals).pack(side=tk.RIGHT)
```

with:

```python
        vitals_fr = ttk.LabelFrame(self, text="MAX30102 — place finger on sensor", padding=8)
        vitals_fr.pack(fill=tk.X, padx=8, pady=4)
        self.vitals_var = tk.StringVar(value="HR: —   SpO2: —")
        ttk.Label(vitals_fr, textvariable=self.vitals_var).pack(side=tk.LEFT)
        self.btn_vitals = ttk.Button(vitals_fr, text="Read vitals", command=self._measure_vitals)
        self.btn_vitals.pack(side=tk.RIGHT)
        self.vitals_progress = ttk.Progressbar(vitals_fr, mode="determinate", length=160)
        self.vitals_progress.pack(side=tk.RIGHT, padx=8)
```

- [ ] **Step 2: Replace `_read_vitals` with the windowed `_measure_vitals`**

Replace the whole `_read_vitals` method (lines 150-161) with:

```python
    def _measure_vitals(self) -> None:
        if not self.bridge or self._busy:
            return
        self._busy = True  # block recording start during the ~15 s measurement
        self.btn_vitals.config(state=tk.DISABLED)
        self.vitals_progress["value"] = 0
        self._log("Measuring vitals — keep fingertip still on the MAX30102...")

        def on_update(v, elapsed: float, duration: float) -> None:
            pct = max(0.0, min(100.0, 100.0 * elapsed / duration))
            if v.valid and v.heart_rate > 0:
                txt = f"Measuring... HR: {v.heart_rate} BPM   SpO2: ~{v.spo2}% (estimate)"
            else:
                txt = "Measuring... keep fingertip still"
            self.after(0, lambda: self.vitals_var.set(txt))
            self.after(0, lambda: self.vitals_progress.config(value=pct))

        def work() -> None:
            try:
                v = self.bridge.measure_vitals(on_update=on_update)
                if v.valid and v.heart_rate > 0:
                    final = f"HR: {v.heart_rate} BPM   SpO2: ~{v.spo2}% (estimate)"
                else:
                    final = "Could not get a reliable reading. Place fingertip gently and keep still."
                self.after(0, lambda: self.vitals_var.set(final))
                self.after(0, lambda: self._log(final))
            finally:
                self._busy = False
                self.after(0, lambda: self.vitals_progress.config(value=0))
                self.after(0, lambda: self.btn_vitals.config(state=tk.NORMAL))

        threading.Thread(target=work, daemon=True).start()
```

- [ ] **Step 3: Stop `_connect` from auto-measuring**

In `_connect`, replace the final call `self._read_vitals()` (line 146) with a hint:

```python
            self.vitals_var.set("Press 'Read vitals' and place fingertip on MAX30102")
```

- [ ] **Step 4: Manual GUI verification**

Run (in `pc/`, with the ESP connected):

```
.\.venv\Scripts\python.exe gui.py
```

- Connect → vitals shows the hint, no long blocking read.
- Click **Read vitals**, place fingertip: the progress bar advances, the line shows live BPM, and on settle it shows `HR: <n> BPM   SpO2: ~<n>% (estimate)`.
- Without a finger, after the window it shows the "Could not get a reliable reading" message.
- Record a voice question and confirm playback still works (audio path untouched) and the reply uses the real vitals when a finger was present.

- [ ] **Step 5: Commit**

```bash
git add pc/gui.py
git commit -m "GUI: windowed vitals measurement with progress and estimate label"
```

---

## Self-Review

**Spec coverage:**
- Honest HR via beat detection → Task 1 (`ppg_compute` peak detection) + Task 2 (real sampling). ✓
- SpO2 ratio-of-ratios, labelled estimate → Task 1 (SpO2 block) + Task 4 (`(estimate)` label). ✓
- No fabricated output (invalid when poor) → Task 1 confidence gate + finger threshold; Task 4 "no reliable reading". ✓
- Windowed UX with progress → Task 3 `measure_vitals` + Task 4 progress bar/live text. ✓
- No protocol change → `MSG_SENSOR` untouched; `sensor_read_vitals` keeps its signature. ✓
- Register change (avg 4→2, rollover ON, clear pointers) → Task 2 `sensor_init`. ✓
- Continuous sampler + spinlock shared state → Task 2. ✓
- LOG-plane debug dump → Task 2 `analyze_and_publish` `ESP_LOGI`; self-test log → Task 1. ✓
- Turkish DEBUG comment removed → Task 2 (full `sensor.c` rewrite, all comments English). ✓
- Manual-count ±5 BPM validation → Task 2 Step 4; no-finger honesty → Task 2 Step 3/4. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code; every command has expected output. ✓

**Type consistency:** `ppg_result_t {heart_rate, spo2, valid}` (Task 1) is mapped field-by-field to `sensor_vitals_t` (Task 2). `vitals_stable(recent_hrs, tolerance, need)` signature matches between the test (Task 3 Step 1) and the implementation (Task 3 Step 3). `measure_vitals(duration_s, settle_s, poll_interval, on_update)` and its `on_update(v, elapsed, duration_s)` callback match between Task 3 and the GUI caller in Task 4. `self.btn_vitals` / `self.vitals_progress` defined in Task 4 Step 1 and used in Steps 2-3. ✓

**Note on spec deviation:** The spec placed the DSP inside `sensor.c`; this plan factors the pure math into `ppg.c` for testability (boot self-test) and isolation. This honours the spec's intent (DSP on the ESP, protocol unchanged) and is the only structural change.
