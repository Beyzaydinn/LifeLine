#pragma once

#include "driver/gpio.h"

/*
 * Hardware map — ESP32-S3-DevKitM-1-N8 (matches Wokwi + breadboard)
 *
 * INMP441 (I2S0)  SD=GPIO2  SCK=41  WS=42  L/R=GND  VDD=3.3V
 * MAX98357A(I2S1) DIN=7  BCLK=5  LRC=6  SD=10(mute)  VIN=5V
 * MAX30102 (I2C) SDA=8  SCL=9  INT=4  addr 0x57  VIN=3.3V
 * WS2812B x8     DIN=38 (330R series)  VCC=5V
 * PC             USB-OTG CDC — Start/Stop record from PC app (no extra button)
 */

/* ── Audio ───────────────────────────────────────────────────────── */
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
#define PROTO_MAX_PAYLOAD       4096
#define PROTO_TX_QUEUE_LEN      16
#define PROTO_RX_QUEUE_LEN      8

/* ── Crypto demo key/IV (course assignment — NOT for production) ─── */
#define CRYPTO_FLAG_ENCRYPTED   0x01

/* ── Task priorities (higher = more urgent) ───────────────────────── */
#define TASK_PRIO_USB_RX        8
#define TASK_PRIO_USB_TX        7
#define TASK_PRIO_AUDIO_IN      7
#define TASK_PRIO_AUDIO_OUT     7
#define TASK_PRIO_FSM           5
#define TASK_PRIO_SENSOR        4
#define TASK_PRIO_LED           3
#define TASK_PRIO_HEARTBEAT     2

#define TASK_STACK_USB          8192
#define TASK_STACK_AUDIO        4096
#define TASK_STACK_DEFAULT      3072
