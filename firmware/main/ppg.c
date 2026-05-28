#include "ppg.h"

#include <math.h>
#include "esp_log.h"

static const char *TAG = "ppg";

/* ---- tunables ---------------------------------------------------------- */
#define FINGER_IR_THRESHOLD   50000.0f   /* raw IR DC below this => no finger */
#define HP_ALPHA              0.97f      /* one-pole high-pass ~0.24 Hz: removes the
                                          * slow baseline rise as finger contact firms;
                                          * the cardiac band passes essentially intact */
#define PEAK_THRESH_FRAC      0.30f      /* fraction of AC peak used as gate. Low because
                                          * the refractory (not amplitude) rejects the
                                          * dicrotic; this catches respiration-modulated
                                          * systolic peaks so beat detection is consistent */
#define REFRACTORY_S          0.50f      /* min beat spacing. Rejects the dicrotic
                                          * notch (~0.38 s after systolic) that doubled
                                          * the rate. Caps HR at ~120 BPM. */
#define STARTUP_SKIP_S        1.0f       /* ignore the oldest ~1 s of the window: the
                                          * HP startup transient from the finger-onset
                                          * sample would otherwise dominate the peak
                                          * threshold for a full window length */
#define MIN_IBI_MS            300.0f     /* 200 BPM ceiling */
#define MAX_IBI_MS            2000.0f    /* 30 BPM floor */
#define MIN_BEATS             4          /* accepted IBIs needed for a reading */
#define MAX_CV                0.40f      /* max IBI coefficient of variation. Loose: the
                                          * median IBI is robust to an occasional missed
                                          * or extra beat, so we accept the window rather
                                          * than flicker to invalid on natural variation */
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

/* One-pole DC blocker / high-pass: y[i] = x[i] - x[i-1] + a*y[i-1]. Removes the
 * baseline, keeps the pulsatile AC component. */
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

/* Peak-to-peak amplitude of a[0..n). */
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
    if (n <= 0 || fs <= 0.0f) return;          /* self-defending public contract */
    if (n > PPG_MAX_SAMPLES) n = PPG_MAX_SAMPLES;
    if (n < (int)(2.0f * fs)) return;          /* need >= ~2 s of data */

    /* 1) finger detection */
    float ir_dc = mean_i32(ir, n);
    if (ir_dc < FINGER_IR_THRESHOLD) return;

    /* 2) AC components */
    hp_filter(ir, n, s_ac_ir);
    hp_filter(red, n, s_ac_red);
    smooth5(s_ac_ir, n, s_smooth);

    /* Analyse [skip..n): drop the HP startup region (the finger-onset sample at
     * the oldest end produces a transient that would corrupt the threshold). */
    int skip = (int)(STARTUP_SKIP_S * fs);
    if (skip >= n - 2) skip = 0;
    int m = n - skip;
    const int32_t *ir_a = ir + skip;
    const int32_t *red_a = red + skip;

    /* 3) peak detection on the smoothed IR AC over [skip..n). The refractory
     * period rejects the dicrotic notch that would otherwise double the rate. */
    float mx = s_smooth[skip];
    for (int i = skip + 1; i < n; i++) if (s_smooth[i] > mx) mx = s_smooth[i];
    float thr = PEAK_THRESH_FRAC * mx;
    int refractory = (int)(REFRACTORY_S * fs);
    if (refractory < 1) refractory = 1;

    int peak_idx[MAX_PEAKS];
    int npeaks = 0;
    int last_peak = skip - refractory - 1;
    for (int i = skip + 1; i < n - 1; i++) {
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

    /* 5) SpO2 estimate (ratio-of-ratios) over the same analysed region */
    int spo2 = 0;
    float ir_dc_a = mean_i32(ir_a, m);
    float red_dc_a = mean_i32(red_a, m);
    /* raw (unsmoothed) AC for both channels keeps the R-ratio unbiased */
    float ac_ir = amplitude(s_ac_ir + skip, m);
    float ac_red = amplitude(s_ac_red + skip, m);
    if (ir_dc_a > 0.0f && red_dc_a > 0.0f && ac_ir > 0.0f) {
        float ratio = (ac_red / red_dc_a) / (ac_ir / ir_dc_a);
        float spo2f = 104.0f - 17.0f * ratio;
        if (spo2f > 100.0f) spo2f = 100.0f;
        if (spo2f < 70.0f) spo2f = 70.0f;
        spo2 = (int)(spo2f + 0.5f);
    }

    out->heart_rate = (uint16_t)bpm;
    out->spo2 = (uint8_t)spo2;
    out->valid = true;
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
             (unsigned)r.heart_rate, r.valid, hr_ok ? "OK" : "FAIL",
             r2.valid, flat_ok ? "OK" : "FAIL");
    return hr_ok && flat_ok;
}
