// Output manager: forwards button events to the chosen transport.
//   OUTPUT_MODE_MQTT  -> publish to Home Assistant via MQTT
//   OUTPUT_MODE_STM32 -> forward to the STM32 over UART (serial JSON lines)
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/uart.h"
#include "output_manager.h"

static const char *TAG = "out";

static esp_mqtt_client_handle_t s_client = NULL;
static config_t s_cfg;
static bool s_serial_installed = false;

// Serial (STM32) output on UART1, 115200 8N1.
#define STM32_UART_PORT      UART_NUM_1
#define STM32_TX_PIN         4
#define STM32_RX_PIN         5
#define STM32_BAUD           115200

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    if (e->event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT connected");
    } else if (e->event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT disconnected");
    }
}

static void serial_init(void)
{
    if (s_serial_installed) return;
    uart_config_t c = {
        .baud_rate = STM32_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(STM32_UART_PORT, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(STM32_UART_PORT, &c));
    ESP_ERROR_CHECK(uart_set_pin(STM32_UART_PORT, STM32_TX_PIN, STM32_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    s_serial_installed = true;
    ESP_LOGI(TAG, "STM32 serial output on UART1 (TX=GPIO%d, RX=GPIO%d, %d baud)",
             STM32_TX_PIN, STM32_RX_PIN, STM32_BAUD);
}

static void serial_send_line(const char *line)
{
    uart_write_bytes(STM32_UART_PORT, line, strlen(line));
    uart_write_bytes(STM32_UART_PORT, "\n", 1);
}

int output_manager_start(const config_t *cfg)
{
    s_cfg = *cfg;
    output_manager_stop();

    if (s_cfg.output_mode == OUTPUT_MODE_STM32) {
        serial_init();
        return 0;
    }

    // MQTT mode.
    if (s_cfg.mqtt_host[0] == '\0') {
        ESP_LOGI(TAG, "no MQTT host configured; MQTT output disabled");
        return 0;
    }
    esp_mqtt_client_config_t m = {0};
    m.broker.address.uri = NULL;
    m.broker.address.hostname = s_cfg.mqtt_host;
    m.broker.address.port = s_cfg.mqtt_port;
    m.credentials.username = s_cfg.mqtt_user[0] ? s_cfg.mqtt_user : NULL;
    m.credentials.authentication.password = s_cfg.mqtt_pass[0] ? s_cfg.mqtt_pass : NULL;

    s_client = esp_mqtt_client_init(&m);
    if (!s_client) return -1;
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(s_client);
    return 0;
}

void output_manager_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    if (s_serial_installed) {
        uart_driver_delete(STM32_UART_PORT);
        s_serial_installed = false;
    }
}

static void mqtt_publish(const char *suffix, const char *value)
{
    if (!s_client || s_cfg.mqtt_host[0] == '\0') return;
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", s_cfg.mqtt_topic, suffix);
    esp_mqtt_client_publish(s_client, topic, value, 0, 0, 0);
}

static const char *event_name(const button_event_t *ev)
{
    switch (ev->event_type) {
        case EVENT_PRESS:      return "press";
        case EVENT_RELEASE:    return "release";
        case EVENT_LONG_PRESS: return "long_press";
        default:               return "unknown";
    }
}

int output_publish_event(const button_event_t *ev)
{
    if (!ev) return -1;
    const char *state = ev->state ? "ON" : "OFF";
    const char *type = event_name(ev);

    if (s_cfg.output_mode == OUTPUT_MODE_STM32) {
        // Serial JSON line for the STM32.
        char line[160];
        snprintf(line, sizeof(line),
                 "{\"id\":\"%s\",\"gpio\":%u,\"event\":\"%s\",\"state\":%u}",
                 ev->button_id, ev->gpio, type, ev->state);
        serial_send_line(line);
    } else {
        // MQTT for Home Assistant.
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "%s/state", ev->button_id);
        mqtt_publish(suffix, state);
        snprintf(suffix, sizeof(suffix), "%s/event", ev->button_id);
        mqtt_publish(suffix, type);
    }

    ESP_LOGI(TAG, "button %s gpio=%d event=%s state=%s (mode=%s)",
             ev->button_id, ev->gpio, type, state,
             s_cfg.output_mode == OUTPUT_MODE_STM32 ? "stm32" : "mqtt");
    return 0;
}

int output_publish_status(const char *json)
{
    if (!json) return -1;
    if (s_cfg.output_mode == OUTPUT_MODE_STM32) {
        serial_send_line(json);
    } else {
        mqtt_publish("status", json);
    }
    return 0;
}
