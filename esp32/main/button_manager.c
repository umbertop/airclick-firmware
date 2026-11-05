// Button manager implementation.
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "button_manager.h"
#include "config_manager.h"
#include "event_manager.h"

static const char *TAG = "btn";

static config_t *s_cfg = NULL;
static bool s_pairing = false;
static char s_pending_id[BUTTON_ID_LEN] = {0};

int button_manager_init(config_t *cfg)
{
    s_cfg = cfg;
    ESP_LOGI(TAG, "button manager init (%d buttons)", cfg->button_count);
    return 0;
}

config_t *button_config(void) { return s_cfg; }

void pairing_start(void) { s_pairing = true; s_pending_id[0] = 0; ESP_LOGI(TAG, "pairing started"); }
void pairing_cancel(void) { s_pairing = false; s_pending_id[0] = 0; }
bool pairing_active(void) { return s_pairing; }
const char *pairing_pending_id(void) { return s_pending_id; }

// Capture an incoming id during pairing (does not add yet; the user confirms
// via the web UI and supplies a name).
void pairing_capture(const char *id)
{
    if (!s_pairing || !id) return;
    strlcpy(s_pending_id, id, sizeof(s_pending_id));
    ESP_LOGI(TAG, "captured pairing id %s", id);
}

int pairing_add(const char *id, const char *name)
{
    if (!s_cfg || !id) return -1;
    if (s_cfg->button_count >= MAX_BUTTONS) return -2;
    for (int i = 0; i < s_cfg->button_count; i++) {
        if (strcmp(s_cfg->buttons[i].id, id) == 0) return -3; // duplicate
    }
    button_t *b = &s_cfg->buttons[s_cfg->button_count++];
    memset(b, 0, sizeof(*b));
    strlcpy(b->id, id, sizeof(b->id));
    if (name) strlcpy(b->name, name, sizeof(b->name));
    else strlcpy(b->name, id, sizeof(b->name));
    b->gpio = LOGICAL_GPIO_MIN;
    b->action = ACTION_MQTT;
    b->enabled = 1;
    config_save(s_cfg);
    s_pairing = false;
    ESP_LOGI(TAG, "paired button %s", id);
    return 0;
}

int button_add(const button_t *b)
{
    if (!s_cfg || !b) return -1;
    if (s_cfg->button_count >= MAX_BUTTONS) return -2;
    s_cfg->buttons[s_cfg->button_count++] = *b;
    config_save(s_cfg);
    return 0;
}

int button_remove(const char *id)
{
    if (!s_cfg) return -1;
    for (int i = 0; i < s_cfg->button_count; i++) {
        if (strcmp(s_cfg->buttons[i].id, id) == 0) {
            // shift remaining down
            for (int j = i; j < s_cfg->button_count - 1; j++) {
                s_cfg->buttons[j] = s_cfg->buttons[j + 1];
            }
            s_cfg->button_count--;
            config_save(s_cfg);
            return 0;
        }
    }
    return -3;
}

int button_edit(const button_t *b)
{
    if (!s_cfg || !b) return -1;
    for (int i = 0; i < s_cfg->button_count; i++) {
        if (strcmp(s_cfg->buttons[i].id, b->id) == 0) {
            s_cfg->buttons[i] = *b;
            config_save(s_cfg);
            return 0;
        }
    }
    return -3;
}

int button_get(const char *id, button_t *out)
{
    if (!s_cfg || !id || !out) return -1;
    for (int i = 0; i < s_cfg->button_count; i++) {
        if (strcmp(s_cfg->buttons[i].id, id) == 0) {
            *out = s_cfg->buttons[i];
            return 0;
        }
    }
    return -3;
}

int button_count(void) { return s_cfg ? s_cfg->button_count : 0; }

int button_list(button_t *out, int max)
{
    if (!s_cfg || !out) return -1;
    int n = s_cfg->button_count < max ? s_cfg->button_count : max;
    for (int i = 0; i < n; i++) out[i] = s_cfg->buttons[i];
    return n;
}

int button_trigger(const char *id, uint8_t event_type, uint8_t state)
{
    if (!s_cfg || !id) return -1;
    button_t b;
    if (button_get(id, &b) != 0) return -3;
    if (!b.enabled) return -4;

    button_event_t ev;
    memset(&ev, 0, sizeof(ev));
    strlcpy(ev.button_id, b.id, sizeof(ev.button_id));
    ev.gpio = b.gpio;
    ev.event_type = event_type;
    ev.state = state;
    return event_publish(&ev);
}
