// Configuration manager: versioned NVS persistence with safe defaults.
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config_manager.h"

static const char *TAG = "cfg";
#define CFG_NAMESPACE "airclick"
#define CFG_SCHEMA_VERSION 2

void config_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->schema_version = CFG_SCHEMA_VERSION;
    strlcpy(cfg->ssid_ap, "AirClick", sizeof(cfg->ssid_ap));
    strlcpy(cfg->pass_ap, "", sizeof(cfg->pass_ap));
    strlcpy(cfg->hostname, "AirClick", sizeof(cfg->hostname));
    cfg->is_ap = true;
    cfg->mqtt_port = 1883;
    strlcpy(cfg->mqtt_topic, "airclick", sizeof(cfg->mqtt_topic));
    cfg->button_count = 0;
    cfg->output_mode = OUTPUT_MODE_MQTT;
    cfg->beep = true;
}

// Read a blob; if the stored schema version differs, treat as unset (migrate to defaults).
static esp_err_t read_blob(nvs_handle_t h, const char *key, void *out, size_t len)
{
    size_t need = len;
    esp_err_t err = nvs_get_blob(h, key, out, &need);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    if (need != len) {
        ESP_LOGW(TAG, "stored %s size mismatch (%d != %d); resetting", key, (int)need, (int)len);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    return err;
}

int config_load(config_t *cfg)
{
    config_defaults(cfg);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS and retrying");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return -1;
    }

    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "cannot open NVS namespace; using defaults");
        return -1;
    }

    // Version gate: if the stored version differs, do not migrate partially — start clean.
    uint8_t stored_version = 0;
    if (nvs_get_u8(h, "schema", &stored_version) == ESP_OK &&
        stored_version == CFG_SCHEMA_VERSION) {
        read_blob(h, "cfg", cfg, sizeof(*cfg));
    } else {
        ESP_LOGW(TAG, "schema version mismatch (%d != %d); using defaults",
                 stored_version, CFG_SCHEMA_VERSION);
    }

    nvs_close(h);

    // Sanity checks / safe defaults.
    if (cfg->button_count > MAX_BUTTONS) cfg->button_count = MAX_BUTTONS;
    if (cfg->mqtt_port == 0) cfg->mqtt_port = 1883;
    if (cfg->mqtt_topic[0] == '\0') strlcpy(cfg->mqtt_topic, "airclick", sizeof(cfg->mqtt_topic));
    if (cfg->hostname[0] == '\0') strlcpy(cfg->hostname, "AirClick", sizeof(cfg->hostname));
    if (strcmp(cfg->hostname, "airclick") == 0) strlcpy(cfg->hostname, "AirClick", sizeof(cfg->hostname));  // migrate old default
    if (cfg->output_mode != OUTPUT_MODE_MQTT && cfg->output_mode != OUTPUT_MODE_STM32)
        cfg->output_mode = OUTPUT_MODE_MQTT;

    return 0;
}

int config_save(const config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "config_save: cannot open NVS");
        return -1;
    }
    nvs_set_u8(h, "schema", CFG_SCHEMA_VERSION);
    nvs_set_blob(h, "cfg", cfg, sizeof(*cfg));
    nvs_commit(h);
    nvs_close(h);
    return 0;
}

int config_factory_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    return 0;
}
