#include "protocol.h"
#include "config.h"
#include <string.h>

uint16_t protocol_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

esp_err_t protocol_frame_pack(uint8_t type, const uint8_t *payload, size_t payload_len,
                              uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t total = 2 + 2 + 1 + payload_len + 2;
    if (total > out_cap) {
        return ESP_ERR_NO_MEM;
    }
    out[0] = PROTO_SYNC0;
    out[1] = PROTO_SYNC1;
    out[2] = (uint8_t)((payload_len >> 8) & 0xFF);
    out[3] = (uint8_t)(payload_len & 0xFF);
    out[4] = type;
    if (payload_len > 0 && payload) {
        memcpy(out + 5, payload, payload_len);
    }
    uint16_t crc = protocol_crc16(out + 4, 1 + payload_len);
    size_t crc_off = 5 + payload_len;
    out[crc_off] = (uint8_t)((crc >> 8) & 0xFF);
    out[crc_off + 1] = (uint8_t)(crc & 0xFF);
    *out_len = total;
    return ESP_OK;
}

void protocol_parser_init(protocol_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

static void dispatch_frame(protocol_parser_t *p, protocol_frame_cb_t cb, void *ctx)
{
    if (!cb) {
        return;
    }
    const uint8_t *payload = (p->payload_len > 0) ? &p->buf[5] : NULL;
    cb(p->type, payload, p->payload_len, ctx);
    protocol_parser_init(p);
}

void protocol_parser_feed(protocol_parser_t *p, const uint8_t *data, size_t len,
                          protocol_frame_cb_t cb, void *ctx)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        if (!p->have_header) {
            if (p->len == 0 && b == PROTO_SYNC0) {
                p->buf[p->len++] = b;
            } else if (p->len == 1 && b == PROTO_SYNC1) {
                p->buf[p->len++] = b;
            } else {
                p->len = 0;
                if (b == PROTO_SYNC0) {
                    p->buf[p->len++] = b;
                }
            }
            if (p->len == 2) {
                p->have_header = true;
                p->need = 2;
            }
            continue;
        }

        if (p->len >= sizeof(p->buf)) {
            protocol_parser_init(p);
            continue;
        }
        p->buf[p->len++] = b;
        if (p->len == 4 && p->need == 2) {
            p->payload_len = ((uint16_t)p->buf[2] << 8) | p->buf[3];
            if (p->payload_len > PROTO_MAX_PAYLOAD) {
                protocol_parser_init(p);
                continue;
            }
            p->need = 1 + p->payload_len + 2;
        }

        if (p->len >= 4 && (p->len - 4) == p->need) {
            p->type = p->buf[4];
            size_t payload_len = p->payload_len;
            uint16_t recv_crc = ((uint16_t)p->buf[5 + payload_len] << 8) |
                                p->buf[5 + payload_len + 1];
            uint16_t calc_crc = protocol_crc16(&p->buf[4], 1 + payload_len);
            if (recv_crc == calc_crc) {
                dispatch_frame(p, cb, ctx);
            } else {
                protocol_parser_init(p);
            }
        }
    }
}

bool protocol_type_encrypted(uint8_t type)
{
    return type == MSG_AUDIO_UP || type == MSG_SENSOR || type == MSG_AUDIO_DOWN;
}
