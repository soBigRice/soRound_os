#pragma once
#include "esp_http_client.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NET_HTTP_OK, NET_HTTP_OFFLINE, NET_HTTP_CANCELED, NET_HTTP_INIT,
    NET_HTTP_CONNECT, NET_HTTP_HEADERS, NET_HTTP_STATUS, NET_HTTP_READ,
    NET_HTTP_INCOMPLETE, NET_HTTP_SIZE, NET_HTTP_TIMEOUT
} network_http_stage_t;
typedef struct {
    network_http_stage_t stage;
    esp_err_t error, transport_error;
    int status, tls_code, tls_flags, socket_errno, attempt;
    size_t bytes;
} network_http_result_t;
typedef bool (*network_http_active_fn)(void *user);

// Caller owns its worker, cancellation token, buffer and parser. At most two
// attempts share one budget; certificate/allocation/size/client errors and most
// 4xx responses are not retried. SDK blocking I/O may outlive a budget check;
// cancellation runs between SDK calls, and the worker retains cleanup ownership.
network_http_result_t network_http_get(const char *tag, const esp_http_client_config_t *config,
    char *body, size_t capacity, int budget_ms, network_http_active_fn active, void *user);
