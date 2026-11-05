// RF capture: interrupt on GPIO10 (the RF receiver pin), record edge timestamps
// with microsecond resolution into a ring buffer, print from a task.
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rf";

#define RF_PIN 10
#define BUF_LEN 4096

typedef struct {
    uint64_t us;
    uint32_t level;
} edge_t;

static edge_t s_buf[BUF_LEN];
static volatile uint32_t s_head = 0;   // next write index
static volatile uint32_t s_count = 0;  // edges recorded (capped)

static void IRAM_ATTR isr(void *arg)
{
    uint32_t i = s_head;
    s_buf[i].us = esp_timer_get_time();
    s_buf[i].level = gpio_get_level(RF_PIN);
    s_head = (i + 1) % BUF_LEN;
    if (s_count < BUF_LEN) s_count++;
}

void app_main(void)
{
    ESP_LOGI(TAG, "RF capture on GPIO%d (interrupt, us resolution)", RF_PIN);

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << RF_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(RF_PIN, isr, NULL);

    ESP_LOGI(TAG, "ready - press the button, then idle");

    uint32_t last_printed = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        uint32_t c = s_count;
        // Print newly recorded edges (between last_printed and c)
        if (c != last_printed) {
            for (uint32_t i = last_printed; i < c; i++) {
                edge_t *e = &s_buf[i % BUF_LEN];
                printf("E %" PRIu64 " %" PRIu32 "\n", e->us, e->level);
            }
            last_printed = c;
        }
    }
}
