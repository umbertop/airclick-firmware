#pragma once
#include "airclick.h"

// Event manager: thread-safe queue of button events plus a subscriber callback.
// The RF receiver / web UI push events; the output manager consumes them.
// This decouples wireless reception from I/O so neither blocks the other.

typedef void (*event_handler_t)(const button_event_t *ev);

// Initialize the event queue. Returns 0 on success.
int event_manager_init(void);

// Push an event (non-blocking; drops if full). Returns 0 on success.
int event_publish(const button_event_t *ev);

// Register the consumer callback (called from the event task).
void event_set_handler(event_handler_t handler);

// Set a callback invoked whenever an event is emitted (for WebSocket fan-out).
void event_set_notifier(event_handler_t notifier);
