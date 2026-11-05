// RF receiver — GPIO10, bit-banged 2.4 GHz button receiver (interrupt + decoder).
//
// Reverse-engineered from µs-resolution captures (see docs/STAGE-1-2-REVERSE-ENGINEERING.md):
//   - Pin: GPIO10 (the only active pin).
//   - Idle: periodic ~14.8 ms HIGH "squelch" pulses with a random-length LOW gap.
//   - Button press: a repeating frame:
//         [ ~11.6 ms LOW sync gap ][ ~36.4 ms data ]
//     Frame period is a very stable ~48 ms. Data is a stream of bit periods of
//     ~1.5 ms each (~24 bits), where the HIGH pulse width encodes the bit:
//         HIGH ~400 us  = '0'
//         HIGH ~1100 us = '1'
//     (the following LOW is the complement: ~1100 us after '0', ~400 us after '1').
//     The receiver's squelch (~14.8 ms HIGH) interleaves with the data at random
//     points and, because it is much longer than any data pulse, is simply
//     skipped. A squelch pulse may swallow one or two adjacent data bits, which
//     is why the same ID is decoded from *many* frames and majority-voted.
//
// Decode strategy:
//   1. Record (timestamp, level) for every edge in the ISR.
//   2. When the line is quiet for >35 ms the press is over.
//   3. Decode only HIGH pulses (short=0, long=1, >1.6 ms skipped as squelch).
//      A LOW gap >5 ms marks a frame boundary.
//   4. Majority-vote the decoded bits across all frames (same position) to
//      recover the clean ID, then hash it into a stable 32-bit signature.
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rf_receiver.h"
#include "button_manager.h"

static const char *TAG = "rf";

#define RF_PIN       10
#define EDGE_MAX     512

#define HIGH_SHORT_US  700   // below this a HIGH pulse is '0'
#define HIGH_LONG_US   1600  // below this a HIGH pulse is '1'; above = squelch
#define SYNC_GAP_US    5000  // LOW gap above this = frame boundary
#define QUIET_US       35000 // line quiet this long = end of press

typedef struct {
    uint32_t us;     // microsecond timestamp of the edge
    uint8_t  level;  // pin level just after this edge (level of the interval that follows)
} edge_t;

static edge_t s_edges[EDGE_MAX];
static volatile int s_count = 0;

// ISR: record the timestamp and the new level for each edge.
static void IRAM_ATTR rf_isr(void *arg)
{
    if (s_count < EDGE_MAX) {
        s_edges[s_count].us = (uint32_t)esp_timer_get_time();
        s_edges[s_count].level = gpio_get_level(RF_PIN);
        s_count++;
    }
}

// ----- decoding -----
#define MAX_FRAMES     16
#define MAX_FRAME_BITS 48

static uint8_t s_frame_bits[MAX_FRAMES][MAX_FRAME_BITS];
static uint8_t s_frame_len[MAX_FRAMES];
static int     s_nframes;

// Decode the captured edges into per-frame bit strings.
static int decode_burst(int n)
{
    s_nframes = 0;
    int fi = 0;
    s_frame_len[0] = 0;

    for (int i = 1; i < n && fi < MAX_FRAMES; i++) {
        uint32_t dur   = s_edges[i].us - s_edges[i - 1].us;
        uint8_t  level = s_edges[i - 1].level;   // level during [i-1, i)

        if (level) {
            // HIGH pulse: width encodes the bit.
            if (dur < HIGH_SHORT_US) {
                if (s_frame_len[fi] < MAX_FRAME_BITS)
                    s_frame_bits[fi][s_frame_len[fi]++] = 0;
            } else if (dur < HIGH_LONG_US) {
                if (s_frame_len[fi] < MAX_FRAME_BITS)
                    s_frame_bits[fi][s_frame_len[fi]++] = 1;
            }
            // else: squelch / delimiter, skip.
        } else {
            // LOW gap: a long one separates frames.
            if (dur > SYNC_GAP_US && s_frame_len[fi] > 0) {
                fi++;
                s_frame_len[fi] = 0;
            }
        }
    }
    if (s_frame_len[fi] > 0) fi++;
    return fi;
}

// Majority-vote across frames and hash the consensus into a 32-bit signature.
// A squelch pulse swallows a few bits at random positions in each frame, so the
// frames differ slightly; voting across several frames recovers the clean ID.
static uint32_t frame_signature(int nframes)
{
    // Majority voting needs several frames to be meaningful.
    if (nframes < 3) return 0;

    // Reference frame = the longest (fewest squelch-swallowed bits).
    int ref = 0;
    for (int f = 1; f < nframes; f++) {
        if (s_frame_len[f] > s_frame_len[ref]) ref = f;
    }
    int ref_len = s_frame_len[ref];
    if (ref_len < 12) return 0;

    uint8_t consensus[MAX_FRAME_BITS];
    int clen = 0;
    for (int pos = 0; pos < ref_len; pos++) {
        int ones = 0, zeros = 0;
        for (int f = 0; f < nframes; f++) {
            if (pos < s_frame_len[f]) {
                if (s_frame_bits[f][pos]) ones++; else zeros++;
            }
        }
        if (ones > zeros)      consensus[clen++] = 1;
        else if (zeros > ones) consensus[clen++] = 0;
        else                   consensus[clen++] = s_frame_bits[ref][pos]; // tie -> reference
    }
    if (clen < 12) return 0;

    // FNV-1a over the consensus bits.
    uint32_t h = 2166136261u;
    for (int i = 0; i < clen; i++) {
        h = (h ^ (consensus[i] + 1)) * 16777619u;
    }
    return h;
}

static void rf_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));
        int n = s_count;
        if (n == 0) continue;

        // Press is complete once the line has been quiet for >QUIET_US.
        uint32_t last = s_edges[(n - 1) % EDGE_MAX].us;
        if ((uint32_t)esp_timer_get_time() - last < QUIET_US) continue;

        int nframes = decode_burst(n);
        uint32_t sig = frame_signature(nframes);
        s_count = 0;

        if (sig != 0) {
            ESP_LOGI(TAG, "button press: %d frames, sig %08" PRIX32, nframes, sig);
            rf_on_frame(sig);
        } else if (nframes >= 3) {
            ESP_LOGW(TAG, "press decoded %d frames but no stable signature", nframes);
        }
    }
}

int rf_receiver_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << RF_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(RF_PIN, rf_isr, NULL);
    xTaskCreate(rf_task, "airclick_rf", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "RF receiver on GPIO%d", RF_PIN);
    return 0;
}

void rf_on_frame(uint32_t button_id)
{
    char id[BUTTON_ID_LEN];
    snprintf(id, sizeof(id), "FLY%08" PRIX32, button_id);

    if (pairing_active()) {
        pairing_capture(id);
        return;
    }

    button_trigger(id, EVENT_PRESS, 1);
}
