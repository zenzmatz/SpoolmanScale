#pragma once

#include <Arduino.h>

bool filamanLookupOrSyncWeight(const char* tag_uuid, int spool_id, float measured_weight_g, const char* context);
bool filamanQuerySpoolById(int spool_id, const char* context);
bool filamanQuerySpoolByTag(const char* tag_uuid, const char* context);
bool filamanLocateCurrentSpool(const char* location_value, String& status_detail);