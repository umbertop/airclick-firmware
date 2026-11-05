#pragma once
#include "airclick.h"

// Load configuration from NVS (or apply safe defaults on first boot).
// Returns 0 on success, non-zero on error (caller may continue with defaults).
int  config_load(config_t *cfg);

// Persist configuration to NVS. Returns 0 on success.
int  config_save(const config_t *cfg);

// Reset configuration to safe defaults (in memory).
void config_defaults(config_t *cfg);

// Reset the NVS namespace to factory state.
int  config_factory_reset(void);
