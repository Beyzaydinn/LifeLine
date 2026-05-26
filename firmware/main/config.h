#pragma once

#include "driver/gpio.h"

/*
 * Hardware map — ESP32-S3-DevKitM-1-N8 (matches Wokwi + breadboard)
 *
 * INMP441 (I2S0)  SD=GPIO2  SCK=41  WS=42  L/R=GND  VDD=3.3V
 * MAX98357A(I2S1) DIN=7  BCLK=5  LRC=6  SD=10(mute)  VIN=5V
 * MAX30102 (I2C) SDA=8  SCL=9  INT=4  addr 0x57  VIN=3.3V
 * WS2812B x8     DIN=38 (330R series)  VCC=5V
 * PC             UART0 (CP210x/CH9102 bridge) @ 921600 baud — Start/Stop
 *                record from PC app (no extra button). USB-OTG CDC was
 *                replaced by UART due to a TinyUSB / Windows generic CDC
 *                ERROR_BAD_COMMAND incompatibility seen on every read.
 */

/* ── Audio ───────────────────────────────────────────────────────── */
/* Hem mikrofon hem hoparlor 16 kHz. 22050 Hz denendi ama MAX98357A /
 * ESP32 I2S driver bu non-yuvarlak rate'i temiz turetemedi, ses
 * tamamen kayboldu. Konusma tempo ve duraksamalari ESP'de degil,
 * PC tarafinda ayarlanir: pc/tts.py icindeki LENGTH_SCALE ve
 * SENTENCE_SILENCE_S sabitleri Piper SynthesisConfig'e gecirilir,
 * cumleler arasina manuel sifir-byte sessizlik enjekte edilir. */
#define AUDIO_SAMPLE_RATE_HZ    16000
#define AUDIO_CHUNK_BYTES       512
#define AUDIO_CHUNK_SAMPLES     (AUDIO_CHUNK_BYTES / 2)

/* ── INMP441 I2S0 RX ─────────────────────────────────────────────── */
#define I2S_MIC_BCLK_GPIO       GPIO_NUM_41
#define I2S_MIC_WS_GPIO         GPIO_NUM_42
#define I2S_MIC_DIN_GPIO        GPIO_NUM_2

/* ── MAX98357A I2S1 TX ───────────────────────────────────────────── */
#define I2S_SPK_BCLK_GPIO       GPIO_NUM_5
#define I2S_SPK_WS_GPIO         GPIO_NUM_6
#define I2S_SPK_DOUT_GPIO       GPIO_NUM_7
#define I2S_SPK_SD_GPIO         GPIO_NUM_10

/* ── MAX30102 I2C ─────────────────────────────────────────────────── */
#define I2C_SDA_GPIO            GPIO_NUM_8
#define I2C_SCL_GPIO            GPIO_NUM_9
#define I2C_INT_GPIO            GPIO_NUM_4
#define MAX30102_I2C_ADDR       0x57

/* ── NeoPixel WS2812B ─────────────────────────────────────────────── */
#define LED_STRIP_GPIO          GPIO_NUM_38
#define LED_STRIP_COUNT         8
#define LED_DEFAULT_BRIGHTNESS  80

/* ── Protocol ─────────────────────────────────────────────────────── */
#define PROTO_MAX_PAYLOAD       1024
#define PROTO_TX_QUEUE_LEN      16
#define PROTO_RX_QUEUE_LEN      8

/* ── Crypto demo key/IV (course assignment — NOT for production) ─── */
#define CRYPTO_FLAG_ENCRYPTED   0x01

/* ── UART link to PC (CP210x/CH9102 bridge) ───────────────────────── */
#define UART_BAUD_RATE          921600

/* ── Task priorities (higher = more urgent) ───────────────────────── */
#define TASK_PRIO_UART_RX       8
#define TASK_PRIO_UART_TX       7
#define TASK_PRIO_AUDIO_IN      7
#define TASK_PRIO_AUDIO_OUT     7
#define TASK_PRIO_FSM           5
#define TASK_PRIO_SENSOR        4
#define TASK_PRIO_LED           3
#define TASK_PRIO_HEARTBEAT     2

#define TASK_STACK_UART         8192
#define TASK_STACK_AUDIO        8192
/* DEFAULT stack icin >=4 KB sart: task_heartbeat ve task_sensor
 * queue_frame'i cagiriyor; queue_frame yerel work[1088] + frame[1056]
 * + frame_item_t (1092) + register window/iç çağrı overhead'i ile
 * ~3.5 KB tepe stack kullaniyor. 3072 byte ile boot sonrasi ilk
 * heartbeat'te stack overflow → _DoubleExceptionVector → TWDT reset.
 * 6144 byte tepenin ~2x'i, guvenli margin birakir. */
#define TASK_STACK_DEFAULT      6144
