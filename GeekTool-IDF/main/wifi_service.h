#pragma once
#include <stdbool.h>

void wifi_service_start(void);
void wifi_service_set_enabled(bool on);
bool wifi_service_enabled(void);
bool wifi_service_initialized(void);
// Router association alone is insufficient: DHCP must have supplied a usable IP.
// This says nothing about a particular DNS name or Internet service being reachable.
bool wifi_service_ready(void);
