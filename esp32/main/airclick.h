// AirClick replacement firmware — shared types and constants.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AIRCLICK_VERSION "0.1.0"
#define MAX_BUTTONS       16
#define BUTTON_ID_LEN     16      // "FLY" + 8 hex + NUL (e.g. FLY12345678)
#define BUTTON_NAME_LEN   33
#define SSID_LEN          33
#define PASS_LEN          65
#define HOST_LEN          65
#define TOPIC_LEN         33

// Logical GPIO output identifiers (map to STM32 PB0..PB9 for wired/GPIO mode,
// or to a Home-Assistant entity for the repurposed mode).
#define LOGICAL_GPIO_MIN   0
#define LOGICAL_GPIO_MAX   9

// Output transport for button events.
#define OUTPUT_MODE_MQTT    0   // publish to Home Assistant via MQTT
#define OUTPUT_MODE_STM32   1   // forward to the STM32 over UART (serial)

typedef enum {
    ACTION_NONE = 0,
    ACTION_GCODE,       // send G-code line (Klipper/Moonraker) — kept for compat
    ACTION_API,         // arbitrary HTTP/Moonraker API call — kept for compat
    ACTION_GPIO,        // toggle a logical GPIO output
    ACTION_MQTT,        // publish an MQTT message (Home Assistant)
    ACTION_MAX
} button_action_t;

typedef enum {
    EVENT_PRESS = 0,
    EVENT_RELEASE,
    EVENT_LONG_PRESS,
    EVENT_MAX
} event_type_t;

// A single paired button.
typedef struct {
    char     id[BUTTON_ID_LEN];      // unique wireless identifier (FLYXXXXXXXX)
    char     name[BUTTON_NAME_LEN];  // friendly name
    uint8_t  gpio;                   // assigned logical GPIO output
    uint8_t  action;                 // button_action_t
    uint8_t  enabled;                // 0/1
} button_t;

// Internal event representation (see requirements §5).
typedef struct {
    char     button_id[BUTTON_ID_LEN];
    uint8_t  gpio;                   // configured logical GPIO
    uint8_t  event_type;             // event_type_t
    uint8_t  state;                  // 0/1 (for press/release)
    uint32_t seq;                    // sequence number
    uint32_t timestamp;              // seconds since boot
} button_event_t;

// Persistent configuration (NVS, versioned).
typedef struct {
    uint8_t  schema_version;
    // network
    char     ssid[SSID_LEN];
    char     pass[PASS_LEN];
    char     ssid_ap[SSID_LEN];
    char     pass_ap[PASS_LEN];
    bool     is_ap;
    char     hostname[HOST_LEN];
    // MQTT output (Home Assistant)
    char     mqtt_host[HOST_LEN];
    uint16_t mqtt_port;
    char     mqtt_user[SSID_LEN];
    char     mqtt_pass[PASS_LEN];
    char     mqtt_topic[TOPIC_LEN];
    // buttons
    button_t buttons[MAX_BUTTONS];
    uint8_t  button_count;
    // output
    uint8_t  output_mode;             // OUTPUT_MODE_MQTT or OUTPUT_MODE_STM32
    // misc
    bool     beep;
} config_t;
