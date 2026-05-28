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
 * Returns true if a synthetic 72 BPM signal is recovered within +/-5 BPM and
 * a flat (no-finger) signal is correctly rejected. Logs the outcome. */
bool ppg_self_test(void);
