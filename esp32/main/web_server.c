// Web server: serves the embedded WebUI + JSON REST API + recent-events feed.
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "web_server.h"
#include "button_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "output_manager.h"
#include "rf_receiver.h"
#include "airclick.h"

static const char *TAG = "web";

// Embedded WebUI (index.html) — declared by EMBED_FILES in CMakeLists.
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// Recent events ring buffer for the dashboard.
#define RECENT_MAX 20
static button_event_t s_recent[RECENT_MAX];
static int s_recent_head = 0;
static int s_recent_count = 0;

static config_t *s_cfg = NULL;

void web_server_record_event(const button_event_t *ev)
{
    if (!ev) return;
    s_recent[s_recent_head] = *ev;
    s_recent_head = (s_recent_head + 1) % RECENT_MAX;
    if (s_recent_count < RECENT_MAX) s_recent_count++;
}

void web_server_notify_event(const button_event_t *ev)
{
    web_server_record_event(ev);
}

// ---------- helpers ----------
static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static char *read_body(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) return NULL;
    char *buf = malloc(total + 1);
    if (!buf) return NULL;
    int off = 0;
    while (off < total) {
        int got = httpd_req_recv(req, buf + off, total - off);
        if (got <= 0) { free(buf); return NULL; }
        off += got;
    }
    buf[off] = '\0';
    return buf;
}

// ---------- dashboard / status ----------
static esp_err_t api_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", AIRCLICK_VERSION);
    cJSON_AddNumberToObject(root, "paired", button_count());

    char wstatus[128];
    wifi_status(wstatus, sizeof(wstatus));
    cJSON_AddStringToObject(root, "wifi", wstatus);

    char ip[16];
    wifi_get_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(root, "ip", ip);

    cJSON_AddNumberToObject(root, "output_mode", button_config()->output_mode);
    cJSON_AddStringToObject(root, "wifi_mode", wifi_is_sta_connected() ? "STA" : "AP");
    char ssid[SSID_LEN];
    wifi_get_ssid(ssid, sizeof(ssid));
    cJSON_AddStringToObject(root, "wifi_ssid", ssid);

    cJSON_AddBoolToObject(root, "pairing", pairing_active());

    cJSON *events = cJSON_AddArrayToObject(root, "events");
    int n = s_recent_count;
    for (int i = 0; i < n; i++) {
        int idx = (s_recent_head - n + i + RECENT_MAX) % RECENT_MAX;
        const button_event_t *e = &s_recent[idx];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", e->button_id);
        cJSON_AddNumberToObject(o, "gpio", e->gpio);
        cJSON_AddStringToObject(o, "event",
            e->event_type == EVENT_PRESS ? "press"
            : e->event_type == EVENT_RELEASE ? "release"
            : e->event_type == EVENT_LONG_PRESS ? "long_press" : "unknown");
        cJSON_AddStringToObject(o, "state", e->state ? "ON" : "OFF");
        cJSON_AddNumberToObject(o, "seq", e->seq);
        cJSON_AddItemToArray(events, o);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t r = send_json(req, json);
    free(json);
    return r;
}

// ---------- buttons ----------
static esp_err_t api_buttons_list(httpd_req_t *req)
{
    button_t list[MAX_BUTTONS];
    int n = button_list(list, MAX_BUTTONS);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", list[i].id);
        cJSON_AddStringToObject(o, "name", list[i].name);
        cJSON_AddNumberToObject(o, "gpio", list[i].gpio);
        cJSON_AddNumberToObject(o, "action", list[i].action);
        cJSON_AddBoolToObject(o, "enabled", list[i].enabled);
        cJSON_AddItemToArray(root, o);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t r = send_json(req, json);
    free(json);
    return r;
}

static esp_err_t api_button_upsert(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }

    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    button_t b;
    memset(&b, 0, sizeof(b));
    cJSON *jid = cJSON_GetObjectItem(o, "id");
    cJSON *jname = cJSON_GetObjectItem(o, "name");
    cJSON *jgpio = cJSON_GetObjectItem(o, "gpio");
    cJSON *jaction = cJSON_GetObjectItem(o, "action");
    cJSON *jenabled = cJSON_GetObjectItem(o, "enabled");

    if (!jid || !jid->valuestring) {
        cJSON_Delete(o);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
        return ESP_FAIL;
    }
    strlcpy(b.id, jid->valuestring, sizeof(b.id));
    if (jname && jname->valuestring) strlcpy(b.name, jname->valuestring, sizeof(b.name));
    b.gpio = jgpio ? (uint8_t)jgpio->valueint : 0;
    b.action = jaction ? (uint8_t)jaction->valueint : ACTION_MQTT;
    b.enabled = jenabled ? (jenabled->valueint ? 1 : 0) : 1;
    cJSON_Delete(o);

    int r = button_edit(&b);
    if (r != 0) r = button_add(&b);
    if (r == 0) {
        send_json(req, "{\"state\":0,\"msg\":\"ok\"}");
    } else {
        send_json(req, "{\"state\":1,\"msg\":\"failed\"}");
    }
    return ESP_OK;
}

static esp_err_t api_button_delete(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }
    cJSON *jid = cJSON_GetObjectItem(o, "id");
    char id[BUTTON_ID_LEN];
    if (!jid || !jid->valuestring) {
        cJSON_Delete(o);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
        return ESP_FAIL;
    }
    strlcpy(id, jid->valuestring, sizeof(id));
    cJSON_Delete(o);

    int r = button_remove(id);
    send_json(req, r == 0 ? "{\"state\":0}" : "{\"state\":1}");
    return ESP_OK;
}

static esp_err_t api_button_test(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }
    cJSON *jid = cJSON_GetObjectItem(o, "id");
    char id[BUTTON_ID_LEN];
    if (!jid || !jid->valuestring) {
        cJSON_Delete(o);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
        return ESP_FAIL;
    }
    strlcpy(id, jid->valuestring, sizeof(id));
    cJSON_Delete(o);

    int r = button_trigger(id, EVENT_PRESS, 1);
    send_json(req, r == 0 ? "{\"state\":0}" : "{\"state\":2,\"msg\":\"unknown button\"}");
    return ESP_OK;
}

// ---------- pairing ----------
static esp_err_t api_pairing_start(httpd_req_t *req)
{
    pairing_start();
    send_json(req, "{\"state\":0,\"msg\":\"pairing\"}");
    return ESP_OK;
}

static esp_err_t api_pairing_cancel(httpd_req_t *req)
{
    pairing_cancel();
    send_json(req, "{\"state\":0,\"msg\":\"cancelled\"}");
    return ESP_OK;
}

static esp_err_t api_pairing_status(httpd_req_t *req)
{
    const char *pending = pairing_pending_id();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "active", pairing_active());
    cJSON_AddStringToObject(o, "id", pending ? pending : "");
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    esp_err_t r = send_json(req, json);
    free(json);
    return r;
}

static esp_err_t api_pairing_add(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }
    cJSON *jid = cJSON_GetObjectItem(o, "id");
    cJSON *jname = cJSON_GetObjectItem(o, "name");
    char id[BUTTON_ID_LEN] = {0};
    char name[BUTTON_NAME_LEN] = {0};
    if (jid && jid->valuestring) strlcpy(id, jid->valuestring, sizeof(id));
    if (jname && jname->valuestring) strlcpy(name, jname->valuestring, sizeof(name));
    cJSON_Delete(o);

    // Fall back to the captured pending id if none supplied.
    if (!id[0]) strlcpy(id, pairing_pending_id(), sizeof(id));
    if (!id[0]) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no id"); return ESP_FAIL; }

    int r = pairing_add(id, name);
    if (r == 0) send_json(req, "{\"state\":0,\"msg\":\"paired\"}");
    else if (r == -3) send_json(req, "{\"state\":4,\"msg\":\"duplicate button id\"}");
    else send_json(req, "{\"state\":5,\"msg\":\"add button failed\"}");
    return ESP_OK;
}

// ---------- config ----------
static esp_err_t api_config_get(httpd_req_t *req)
{
    config_t *c = button_config();
    cJSON *o = cJSON_CreateObject();
    cJSON *net = cJSON_AddObjectToObject(o, "net");
    cJSON_AddStringToObject(net, "ssid", c->ssid);
    cJSON_AddStringToObject(net, "pass", c->pass);
    cJSON_AddStringToObject(net, "ssid_ap", c->ssid_ap);
    cJSON_AddStringToObject(net, "pass_ap", c->pass_ap);
    cJSON_AddBoolToObject(net, "is_ap", c->is_ap);
    cJSON_AddStringToObject(net, "hostname", c->hostname);
    cJSON *mqtt = cJSON_AddObjectToObject(o, "mqtt");
    cJSON_AddStringToObject(mqtt, "host", c->mqtt_host);
    cJSON_AddNumberToObject(mqtt, "port", c->mqtt_port);
    cJSON_AddStringToObject(mqtt, "user", c->mqtt_user);
    cJSON_AddStringToObject(mqtt, "pass", c->mqtt_pass);
    cJSON_AddStringToObject(mqtt, "topic", c->mqtt_topic);
    cJSON_AddNumberToObject(o, "output_mode", c->output_mode);
    cJSON_AddBoolToObject(o, "beep", c->beep);

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char macstr[18];
    snprintf(macstr, sizeof(macstr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(o, "mac", macstr);

    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    esp_err_t r = send_json(req, json);
    free(json);
    return r;
}

static esp_err_t api_config_set(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    config_t *c = button_config();
    cJSON *net = cJSON_GetObjectItem(o, "net");
    if (net) {
        if (cJSON_GetObjectItem(net, "ssid")) strlcpy(c->ssid, cJSON_GetObjectItem(net, "ssid")->valuestring, sizeof(c->ssid));
        if (cJSON_GetObjectItem(net, "pass")) strlcpy(c->pass, cJSON_GetObjectItem(net, "pass")->valuestring, sizeof(c->pass));
        if (cJSON_GetObjectItem(net, "ssid_ap")) strlcpy(c->ssid_ap, cJSON_GetObjectItem(net, "ssid_ap")->valuestring, sizeof(c->ssid_ap));
        if (cJSON_GetObjectItem(net, "pass_ap")) strlcpy(c->pass_ap, cJSON_GetObjectItem(net, "pass_ap")->valuestring, sizeof(c->pass_ap));
        if (cJSON_GetObjectItem(net, "is_ap")) c->is_ap = cJSON_GetObjectItem(net, "is_ap")->valueint ? true : false;
        if (cJSON_GetObjectItem(net, "hostname")) strlcpy(c->hostname, cJSON_GetObjectItem(net, "hostname")->valuestring, sizeof(c->hostname));
    }
    cJSON *mqtt = cJSON_GetObjectItem(o, "mqtt");
    if (mqtt) {
        if (cJSON_GetObjectItem(mqtt, "host")) strlcpy(c->mqtt_host, cJSON_GetObjectItem(mqtt, "host")->valuestring, sizeof(c->mqtt_host));
        if (cJSON_GetObjectItem(mqtt, "port")) c->mqtt_port = (uint16_t)cJSON_GetObjectItem(mqtt, "port")->valueint;
        if (cJSON_GetObjectItem(mqtt, "user")) strlcpy(c->mqtt_user, cJSON_GetObjectItem(mqtt, "user")->valuestring, sizeof(c->mqtt_user));
        if (cJSON_GetObjectItem(mqtt, "pass")) strlcpy(c->mqtt_pass, cJSON_GetObjectItem(mqtt, "pass")->valuestring, sizeof(c->mqtt_pass));
        if (cJSON_GetObjectItem(mqtt, "topic")) strlcpy(c->mqtt_topic, cJSON_GetObjectItem(mqtt, "topic")->valuestring, sizeof(c->mqtt_topic));
    }
    if (cJSON_GetObjectItem(o, "output_mode")) {
        int m = cJSON_GetObjectItem(o, "output_mode")->valueint;
        c->output_mode = (m == OUTPUT_MODE_STM32) ? OUTPUT_MODE_STM32 : OUTPUT_MODE_MQTT;
    }
    cJSON_Delete(o);

    config_save(c);
    wifi_manager_apply(c);
    output_manager_stop();
    output_manager_start(c);
    send_json(req, "{\"state\":0,\"msg\":\"saved\"}");
    return ESP_OK;
}

// ---------- wifi scan ----------
static esp_err_t api_wifi_scan(httpd_req_t *req)
{
    esp_wifi_scan_start(NULL, true);
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    wifi_ap_record_t aps[16];
    esp_wifi_scan_get_ap_records(&count, aps);
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (char *)aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
        cJSON_AddItemToArray(root, o);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t r = send_json(req, json);
    free(json);
    return r;
}

// ---------- system ----------
static esp_err_t api_reboot(httpd_req_t *req)
{
    send_json(req, "{\"state\":0,\"msg\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(300));  // allow the response to flush before rebooting
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_factory_reset(httpd_req_t *req)
{
    config_factory_reset();
    send_json(req, "{\"state\":0,\"msg\":\"factory reset\"}");
    vTaskDelay(pdMS_TO_TICKS(300));  // allow the response to flush before rebooting
    esp_restart();
    return ESP_OK;
}

// ---------- index ----------
static esp_err_t serve_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static const httpd_uri_t s_uris[] = {
    { .uri = "/",                     .method = HTTP_GET,  .handler = serve_index },
    { .uri = "/index.html",           .method = HTTP_GET,  .handler = serve_index },
    { .uri = "/api/status",           .method = HTTP_GET,  .handler = api_status },
    { .uri = "/api/buttons",          .method = HTTP_GET,  .handler = api_buttons_list },
    { .uri = "/api/buttons",          .method = HTTP_POST, .handler = api_button_upsert },
    { .uri = "/api/buttons/delete",   .method = HTTP_POST, .handler = api_button_delete },
    { .uri = "/api/buttons/test",     .method = HTTP_POST, .handler = api_button_test },
    { .uri = "/api/pairing/start",    .method = HTTP_POST, .handler = api_pairing_start },
    { .uri = "/api/pairing/cancel",   .method = HTTP_POST, .handler = api_pairing_cancel },
    { .uri = "/api/pairing/status",   .method = HTTP_GET,  .handler = api_pairing_status },
    { .uri = "/api/pairing/add",      .method = HTTP_POST, .handler = api_pairing_add },
    { .uri = "/api/config",           .method = HTTP_GET,  .handler = api_config_get },
    { .uri = "/api/config",           .method = HTTP_POST, .handler = api_config_set },
    { .uri = "/api/wifi/scan",        .method = HTTP_GET,  .handler = api_wifi_scan },
    { .uri = "/api/system/reboot",    .method = HTTP_POST, .handler = api_reboot },
    { .uri = "/api/system/factory",   .method = HTTP_POST, .handler = api_factory_reset },
};

int web_server_start(void)
{
    s_cfg = button_config();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 32;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return -1;
    }
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
        httpd_register_uri_handler(server, &s_uris[i]);
    }
    ESP_LOGI(TAG, "web server started on port 80");
    return 0;
}
