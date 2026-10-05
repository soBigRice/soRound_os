#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "mbedtls/x509.h"

// ESP-TLS stores -ret (positive); normalize both backend and raw mbedTLS signs.
// Unsigned subtraction also handles INT_MIN without signed overflow.
static inline unsigned ota_tls_error_magnitude(int code) {
    return code < 0 ? 0u - (unsigned)code : (unsigned)code;
}
static inline bool ota_tls_certificate_error(int code) {
    unsigned magnitude = ota_tls_error_magnitude(code);
    return magnitude == ota_tls_error_magnitude(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) ||
           magnitude == ota_tls_error_magnitude(MBEDTLS_ERR_X509_FATAL_ERROR);
}

#define OTA_UPDATE_ATTEMPTS 3
typedef enum {
    OTA_IDLE, OTA_CHECKING, OTA_HEADER, OTA_RUNNING, OTA_RETRYING,
    OTA_VERIFYING, OTA_OK, OTA_FAIL, OTA_UPTODATE
} ota_state_t;

typedef struct {
    ota_state_t state, failed_at;
    esp_err_t error;
    int pct, attempt, http_status, tls_code, tls_flags;
    char version[32];
} ota_status_t;

// 同步运行于唯一 OTA worker。回调只发布状态,不得操作 LVGL。
typedef void (*ota_status_cb)(const ota_status_t *status, void *user);
ota_status_t ota_update_run(const char *url, ota_status_cb callback, void *user);
