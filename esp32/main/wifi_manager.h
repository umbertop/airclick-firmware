#pragma once
#include "airclick.h"

// Wi-Fi manager: starts an access point for initial configuration, and can
// connect to an existing network (STA) once credentials are set.

// Start Wi-Fi in the configured mode (AP or STA). Returns 0 on success.
int wifi_manager_start(const config_t *cfg);

// Convenience: reconfigure and reconnect STA after settings change.
int wifi_manager_apply(const config_t *cfg);

// Get current Wi-Fi status summary (AP or STA, IP address).
void wifi_status(char *buf, size_t len);

// Get the device's current IP address (STA IP when connected, else AP IP,
// else "—"). Buffer must be at least 16 bytes.
void wifi_get_ip(char *buf, size_t len);

// True when the device is connected to a home network (STA has an IP).
bool wifi_is_sta_connected(void);

// Get the active SSID (the connected STA SSID, else the AP SSID). Buffer must
// be at least 33 bytes.
void wifi_get_ssid(char *buf, size_t len);
