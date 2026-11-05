#pragma once
#include "airclick.h"

// Button manager: paired-button registry + pairing session + event generation.
// In the real device, RF frames arrive via rf_receiver and are matched against
// the paired list here. For now, pairing can be driven by the Web UI and
// simulated button events can be injected for testing.

// Initialize the button manager (loads config).
int  button_manager_init(config_t *cfg);

// --- pairing ---
// Start a pairing window (listening for a new button id).
void pairing_start(void);
// Complete a pairing with an incoming id (returns 0 and adds the button).
int  pairing_add(const char *id, const char *name);
void pairing_cancel(void);
bool pairing_active(void);
// Return the most recently captured (pending) id during an active pairing
// session, or an empty string if none captured yet.
const char *pairing_pending_id(void);
// Record an incoming id during pairing (does not add yet).
void pairing_capture(const char *id);

// --- registry ---
int  button_add(const button_t *b);
int  button_remove(const char *id);
int  button_edit(const button_t *b);
int  button_get(const char *id, button_t *out);
int  button_count(void);
int  button_list(button_t *out, int max);

// --- events ---
// Simulate a button press/release for a given id (used by the Web UI "test"
// button and by the RF receiver when a real frame is decoded).
int  button_trigger(const char *id, uint8_t event_type, uint8_t state);

// Access the shared config (for the web server / other modules).
config_t *button_config(void);
