#pragma once
#include "airclick.h"

// Output manager: forwards button events to Home Assistant / other systems.
// The primary transport is MQTT (with Home Assistant auto-discovery). Events
// are published as e.g.:
//   <prefix>/<button_id>/state  -> "ON"/"OFF"
//   <prefix>/<button_id>/event  -> "press"/"release"/"long_press"

// Initialize and (re)configure MQTT from config. Returns 0 on success.
int output_manager_start(const config_t *cfg);

// Publish a button event to MQTT.
int output_publish_event(const button_event_t *ev);

// Publish a raw JSON string to the event topic (used for status/heartbeat).
int output_publish_status(const char *json);

// Stop MQTT (on reconfigure / reboot).
void output_manager_stop(void);
