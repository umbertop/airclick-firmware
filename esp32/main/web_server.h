#pragma once
#include "airclick.h"

// Web server: serves the embedded WebUI and a small JSON REST API, plus a
// lightweight event feed (recent button events for the dashboard).

// Start the HTTP server (port 80). Returns 0 on success.
int web_server_start(void);

// Notify the server of a button event (for the recent-events feed).
void web_server_notify_event(const button_event_t *ev);

// Store a recent event (from the event notifier).
void web_server_record_event(const button_event_t *ev);
