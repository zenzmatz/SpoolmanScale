#pragma once

#include <stdint.h>

enum BackendMode : uint8_t {
  BACKEND_SPOOLMAN = 0,
  BACKEND_FILAMAN  = 1,
};

enum FilamanTextTarget : uint8_t {
  FILAMAN_FIELD_URL = 0,
  FILAMAN_FIELD_TOKEN = 1,
  FILAMAN_FIELD_CODE = 2,
  FILAMAN_FIELD_LOCATION = 3,
};

enum SpoolmanTextTarget : uint8_t {
  SPOOLMAN_FIELD_TOKEN = 0,
  SPOOLMAN_FIELD_CODE = 1,
};