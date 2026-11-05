// Event manager implementation (FreeRTOS queue + consumer task).
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_manager.h"

static const char *TAG = "event";

#define EVENT_QUEUE_LEN 32
static QueueHandle_t s_queue;
static event_handler_t s_handler = NULL;
static event_handler_t s_notifier = NULL;
static uint32_t s_seq = 0;

static void event_task(void *arg)
{
    button_event_t ev;
    while (1) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) == pdTRUE) {
            if (s_notifier) s_notifier(&ev);
            if (s_handler) s_handler(&ev);
        }
    }
}

int event_manager_init(void)
{
    s_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(button_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return -1;
    }
    xTaskCreate(event_task, "airclick_events", 3072, NULL, 5, NULL);
    return 0;
}

int event_publish(const button_event_t *ev)
{
    if (!s_queue) return -1;
    button_event_t copy = *ev;
    copy.seq = ++s_seq;
    copy.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    if (xQueueSend(s_queue, &copy, 0) != pdTRUE) {
        return -1;
    }
    return 0;
}

void event_set_handler(event_handler_t handler) { s_handler = handler; }
void event_set_notifier(event_handler_t notifier) { s_notifier = notifier; }
