#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t crypto_init(void);
bool crypto_self_test(void);

esp_err_t pkcs7_pad(const uint8_t *input, size_t input_len,
                    uint8_t *output, size_t output_cap, size_t *output_len);

esp_err_t pkcs7_unpad(const uint8_t *input, size_t input_len,
                      uint8_t *output, size_t output_cap, size_t *output_len);

esp_err_t aes_encrypt_cbc(const uint8_t *plaintext, size_t plaintext_len,
                          const uint8_t iv[16],
                          uint8_t *ciphertext, size_t ciphertext_cap,
                          size_t *ciphertext_len);

esp_err_t aes_decrypt_cbc(const uint8_t *ciphertext, size_t ciphertext_len,
                          const uint8_t iv[16],
                          uint8_t *plaintext, size_t plaintext_cap,
                          size_t *plaintext_len);

/* Wrap: [FLAGS|IV|CT] for protocol */
esp_err_t crypto_encrypt_payload(const uint8_t *plain, size_t plain_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len);

esp_err_t crypto_decrypt_payload(const uint8_t *blob, size_t blob_len,
                                 uint8_t *plain, size_t plain_cap, size_t *plain_len);
