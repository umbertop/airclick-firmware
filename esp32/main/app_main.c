// AirClick replacement firmware — entry point.
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "airclick.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "button_manager.h"
#include "event_manager.h"
#include "output_manager.h"
#include "web_server.h"
#include "rf_receiver.h"

static const char *TAG = "app";

static config_t g_cfg;

// Event consumer: forward button events to the configured output (MQTT).
static void on_event(const button_event_t *ev)
{
    output_publish_event(ev);
}

// Event notifier: fan out to the web server's recent-events feed.
static void on_event_notify(const button_event_t *ev)
{
    web_server_notify_event(ev);
}

void app_main(void)
{
    ESP_LOGI(TAG, "AirClick replacement firmware v%s booting", AIRCLICK_VERSION);

    // Load configuration (NVS with safe defaults).
    config_load(&g_cfg);

    // Event queue + consumers.
    event_manager_init();
    event_set_handler(on_event);
    event_set_notifier(on_event_notify);

    // Button registry.
    button_manager_init(&g_cfg);

    // Wi-Fi (AP for config + optional STA).
    wifi_manager_start(&g_cfg);

    // RF receiver (stub until the real bit-banged protocol is ported).
    rf_receiver_init();

    // Output (MQTT for Home Assistant).
    output_manager_start(&g_cfg);

    // Web UI + REST API.
    web_server_start();

    ESP_LOGI(TAG, "ready. AP SSID: %s", g_cfg.ssid_ap[0] ? g_cfg.ssid_ap : "AirClick");
}
