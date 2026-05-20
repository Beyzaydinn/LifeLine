#include "crypto_aes.h"
#include "config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_random.h"
#include "mbedtls/aes.h"

static const char *TAG = "crypto";

static const uint8_t AES_KEY[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint8_t AES_IV_DEFAULT[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

esp_err_t crypto_init(void)
{
    return ESP_OK;
}

esp_err_t pkcs7_pad(const uint8_t *input, size_t input_len,
                    uint8_t *output, size_t output_cap, size_t *output_len)
{
    if (!input || !output || !output_len || input_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t pad = 16 - (input_len % 16);
    size_t total = input_len + pad;
    if (total > output_cap) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(output, input, input_len);
    memset(output + input_len, pad, pad);
    *output_len = total;
    return ESP_OK;
}

esp_err_t pkcs7_unpad(const uint8_t *input, size_t input_len,
                      uint8_t *output, size_t output_cap, size_t *output_len)
{
    if (!input || !output || !output_len || input_len == 0 || (input_len % 16) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t pad = input[input_len - 1];
    if (pad < 1 || pad > 16 || pad > input_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < pad; i++) {
        if (input[input_len - 1 - i] != pad) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    size_t plain_len = input_len - pad;
    if (plain_len > output_cap) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(output, input, plain_len);
    *output_len = plain_len;
    return ESP_OK;
}

esp_err_t aes_encrypt_cbc(const uint8_t *plaintext, size_t plaintext_len,
                          const uint8_t iv[16],
                          uint8_t *ciphertext, size_t ciphertext_cap,
                          size_t *ciphertext_len)
{
    if (!plaintext || !iv || !ciphertext || !ciphertext_len) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t padded[PROTO_MAX_PAYLOAD + 16];
    size_t padded_len = 0;
    ESP_RETURN_ON_ERROR(pkcs7_pad(plaintext, plaintext_len, padded, sizeof(padded), &padded_len),
                        TAG, "pad failed");

    if (padded_len > ciphertext_cap) {
        return ESP_ERR_NO_MEM;
    }

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_enc(&ctx, AES_KEY, 256);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return ESP_FAIL;
    }

    uint8_t iv_work[16];
    memcpy(iv_work, iv, 16);
    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, padded_len, iv_work, padded, ciphertext);
    mbedtls_aes_free(&ctx);
    if (ret != 0) {
        return ESP_FAIL;
    }
    *ciphertext_len = padded_len;
    return ESP_OK;
}

esp_err_t aes_decrypt_cbc(const uint8_t *ciphertext, size_t ciphertext_len,
                          const uint8_t iv[16],
                          uint8_t *plaintext, size_t plaintext_cap,
                          size_t *plaintext_len)
{
    if (!ciphertext || !iv || !plaintext || !plaintext_len ||
        ciphertext_len == 0 || (ciphertext_len % 16) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t decrypted[PROTO_MAX_PAYLOAD + 16];
    if (ciphertext_len > sizeof(decrypted)) {
        return ESP_ERR_NO_MEM;
    }

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_dec(&ctx, AES_KEY, 256);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return ESP_FAIL;
    }

    uint8_t iv_work[16];
    memcpy(iv_work, iv, 16);
    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, ciphertext_len, iv_work, ciphertext, decrypted);
    mbedtls_aes_free(&ctx);
    if (ret != 0) {
        return ESP_FAIL;
    }

    return pkcs7_unpad(decrypted, ciphertext_len, plaintext, plaintext_cap, plaintext_len);
}

esp_err_t crypto_encrypt_payload(const uint8_t *plain, size_t plain_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!plain || !out || !out_len || out_cap < 17 + 16) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t iv[16];
    esp_fill_random(iv, 16);

    size_t ct_cap = out_cap - 17;
    size_t ct_len = 0;
    ESP_RETURN_ON_ERROR(aes_encrypt_cbc(plain, plain_len, iv, out + 17, ct_cap, &ct_len),
                        TAG, "encrypt failed");

    out[0] = CRYPTO_FLAG_ENCRYPTED;
    memcpy(out + 1, iv, 16);
    *out_len = 1 + 16 + ct_len;
    return ESP_OK;
}

esp_err_t crypto_decrypt_payload(const uint8_t *blob, size_t blob_len,
                                 uint8_t *plain, size_t plain_cap, size_t *plain_len)
{
    if (!blob || !plain || !plain_len || blob_len < 17) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((blob[0] & CRYPTO_FLAG_ENCRYPTED) == 0) {
        if (blob_len > plain_cap) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(plain, blob, blob_len);
        *plain_len = blob_len;
        return ESP_OK;
    }
    const uint8_t *iv = blob + 1;
    const uint8_t *ct = blob + 17;
    size_t ct_len = blob_len - 17;
    return aes_decrypt_cbc(ct, ct_len, iv, plain, plain_cap, plain_len);
}

bool crypto_self_test(void)
{
    const char *test_msg = "Hello ESP32-S3! This is an AES-256-CBC test message.";
    size_t msg_len = strlen(test_msg);

    uint8_t ct[128];
    size_t ct_len = 0;
    if (aes_encrypt_cbc((const uint8_t *)test_msg, msg_len, AES_IV_DEFAULT, ct, sizeof(ct), &ct_len) != ESP_OK) {
        ESP_LOGE(TAG, "AES self-test encrypt FAILED");
        return false;
    }

    uint8_t pt[128];
    size_t pt_len = 0;
    if (aes_decrypt_cbc(ct, ct_len, AES_IV_DEFAULT, pt, sizeof(pt), &pt_len) != ESP_OK) {
        ESP_LOGE(TAG, "AES self-test decrypt FAILED");
        return false;
    }
    pt[pt_len] = '\0';

    if (strcmp((char *)pt, test_msg) != 0) {
        ESP_LOGE(TAG, "AES self-test mismatch");
        return false;
    }

    ESP_LOGI(TAG, "AES self-test: SUCCESS");
    ESP_LOGI(TAG, "Original: %s", test_msg);
    ESP_LOG_BUFFER_HEX(TAG, ct, ct_len);
    ESP_LOGI(TAG, "Decrypted: %s", pt);
    return true;
}
