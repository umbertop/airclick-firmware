#pragma once
#include "airclick.h"

// RF receiver — pluggable wireless-button front-end.
//
// The stock AirClick receives wireless buttons (CR927 coin-cell, 2.4 GHz)
// through a dedicated RF receiver whose demodulated output is a bit-banged
// digital signal on an ESP32-C3 GPIO, decoded by an interrupt handler.
//
// The exact GPIO pin and bit-stream timing/encoding could not be recovered
// from the stripped firmware without a schematic or logic analyzer. To keep
// the firmware working today, this module exposes a clean interface:
//
//   - rf_receiver_init()  : initialise the receiver (real driver or stub)
//   - rf_on_frame()       : called when a button frame is decoded, carrying
//                           the 32-bit button ID rendered as "FLYXXXXXXXX".
//
// To add the real protocol later, implement rf_receiver_init() to configure
// the GPIO, install an ISR, and call rf_on_frame() with the decoded ID.

// Initialize the receiver. Returns 0 on success.
int rf_receiver_init(void);

// Decoded-frame callback: build the "FLYXXXXXXXX" id and forward to the
// button manager (matches against the paired list; during pairing it is
// captured as a new button).
void rf_on_frame(uint32_t button_id);
