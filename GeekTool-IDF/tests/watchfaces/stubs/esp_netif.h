#pragma once
#include <stdint.h>
typedef struct {uint32_t addr;} wf_ip_t;
typedef struct {wf_ip_t ip;} esp_netif_ip_info_t;
typedef void esp_netif_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((ip)->addr&255u), (((ip)->addr>>8)&255u), (((ip)->addr>>16)&255u), (((ip)->addr>>24)&255u)
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip);
