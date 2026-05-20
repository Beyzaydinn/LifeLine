#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

#define PROTO_SYNC0           0xAA
#define PROTO_SYNC1           0x55

#define MSG_AUDIO_UP          0x01
#define MSG_SENSOR            0x02
#define MSG_BUTTON            0x03
#define MSG_STATUS            0x04
#define MSG_WAKE_DETECTED     0x05
#define MSG_HEARTBEAT         0x06
#define MSG_AUDIO_DOWN        0x10
#define MSG_LED_CMD           0x11
#define MSG_PLAYBACK_END      0x12
#define MSG_RESET             0x13
#define MSG_CRYPTO_CAP        0x14
#define MSG_START_RECORD      0x15
#define MSG_STOP_RECORD       0x16
#define MSG_READ_SENSOR       0x17

uint16_t protocol_crc16(const uint8_t *data, size_t len);

esp_err_t protocol_frame_pack(uint8_t type, const uint8_t *payload, size_t payload_len,
                              uint8_t *out, size_t out_cap, size_t *out_len);

typedef void (*protocol_frame_cb_t)(uint8_t type, const uint8_t *payload, size_t len, void *ctx);

typedef struct {
    uint8_t buf[PROTO_MAX_PAYLOAD + 16];
    size_t len;
    size_t need;
    bool have_header;
    uint8_t type;
    uint16_t payload_len;
} protocol_parser_t;

void protocol_parser_init(protocol_parser_t *p);
void protocol_parser_feed(protocol_parser_t *p, const uint8_t *data, size_t len,
                          protocol_frame_cb_t cb, void *ctx);

bool protocol_type_encrypted(uint8_t type);
