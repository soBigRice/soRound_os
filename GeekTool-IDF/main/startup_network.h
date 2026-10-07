#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef enum { STARTUP_NET_READY,STARTUP_NET_OFFLINE,STARTUP_NET_FAILED,STARTUP_NET_TIMEOUT } startup_network_state_t;
typedef struct {
    startup_network_state_t state;
    esp_err_t error,transport_error;
    int http_status,tls_code,tls_flags,socket_errno;
} startup_network_result_t;
// One read-only HTTPS HEAD to the selected OTA origin. Wait at most 8.5s;
// a blocked SDK call retains worker ownership until cleanup, without touching UI.
startup_network_result_t startup_network_check(bool beta);
