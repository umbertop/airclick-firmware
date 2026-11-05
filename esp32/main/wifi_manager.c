// Wi-Fi manager — AP + STA coexistence.
//
// The access point is ALWAYS enabled so the configuration web UI is always
// reachable (192.168.4.1). If the user configured a home network ("join
// existing Wi-Fi"), the device additionally connects as a station so it can
// reach the MQTT broker. This avoids the classic failure where switching to
// station mode with bad credentials bricks the device.
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "wifi_manager.h"

static const char *TAG = "wifi";

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

static void apply_ap(const config_t *cfg)
{
    wifi_config_t ap = {0};
    const char *ssid = cfg->ssid_ap[0] ? cfg->ssid_ap : "AirClick";
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    strlcpy((char *)ap.ap.password, cfg->pass_ap, sizeof(ap.ap.password));
    ap.ap.max_connection = 4;
    ap.ap.authmode = cfg->pass_ap[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
}

int wifi_manager_start(const config_t *cfg)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // Set the DHCP hostname so the device shows up as "AirClick" in the router.
    const char *host = cfg->hostname[0] ? cfg->hostname : "AirClick";
    esp_netif_set_hostname(s_sta_netif, host);
    esp_netif_set_hostname(s_ap_netif, host);

    apply_ap(cfg);
    ESP_ERROR_CHECK(esp_wifi_start());

    // Optionally join the home network.
    if (!cfg->is_ap && cfg->ssid[0]) {
        wifi_config_t sta = {0};
        strlcpy((char *)sta.sta.ssid, cfg->ssid, sizeof(sta.sta.ssid));
        strlcpy((char *)sta.sta.password, cfg->pass, sizeof(sta.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_ERROR_CHECK(esp_wifi_connect());
        ESP_LOGI(TAG, "connecting to SSID %s", cfg->ssid);
    } else {
        ESP_LOGI(TAG, "AP-only mode (SSID %s)", cfg->ssid_ap[0] ? cfg->ssid_ap : "AirClick");
    }
    return 0;
}

int wifi_manager_apply(const config_t *cfg)
{
    apply_ap(cfg);

    if (!cfg->is_ap && cfg->ssid[0]) {
        wifi_config_t sta = {0};
        strlcpy((char *)sta.sta.ssid, cfg->ssid, sizeof(sta.sta.ssid));
        strlcpy((char *)sta.sta.password, cfg->pass, sizeof(sta.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        esp_wifi_connect();
        ESP_LOGI(TAG, "reconnecting to SSID %s", cfg->ssid);
    } else {
        esp_wifi_disconnect();
    }
    return 0;
}

void wifi_status(char *buf, size_t len)
{
    char ip[16] = {0};
    esp_netif_ip_info_t info;

    wifi_ap_record_t ap = {0};
    if (s_sta_netif && esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0]) {
        if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK) {
            esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));
        }
        snprintf(buf, len, "STA ssid=%s ip=%s", (char *)ap.ssid, ip);
        return;
    }

    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
        esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));
    }
    snprintf(buf, len, "AP mode, ip=%s", ip);
}

void wifi_get_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t info;
    wifi_ap_record_t ap = {0};
    if (s_sta_netif && esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0] &&
        esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK && info.ip.addr != 0) {
        esp_ip4addr_ntoa(&info.ip, buf, len);
        return;
    }
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK && info.ip.addr != 0) {
        esp_ip4addr_ntoa(&info.ip, buf, len);
        return;
    }
    strlcpy(buf, "—", len);
}

bool wifi_is_sta_connected(void)
{
    esp_netif_ip_info_t info;
    wifi_ap_record_t ap = {0};
    return s_sta_netif &&
           esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0] &&
           esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK && info.ip.addr != 0;
}

void wifi_get_ssid(char *buf, size_t len)
{
    wifi_ap_record_t ap = {0};
    if (s_sta_netif && esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0]) {
        strlcpy(buf, (char *)ap.ssid, len);
        return;
    }
    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK && cfg.ap.ssid_len) {
        strlcpy(buf, (char *)cfg.ap.ssid, len);
        return;
    }
    strlcpy(buf, "AirClick", len);
}
