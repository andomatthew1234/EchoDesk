#ifndef BEDSIDE_STORE_H
#define BEDSIDE_STORE_H

#include "bedside_mode.h"

// Bedside owns a dedicated NVS namespace. It never reads or mutates AppConfig.
bool bedside_store_load(BedsideConfig &config, uint32_t &revision);
bool bedside_store_save(const BedsideConfig &config, uint32_t revision);

#endif // BEDSIDE_STORE_H
