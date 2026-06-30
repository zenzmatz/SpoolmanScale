#pragma once

#include <stdint.h>

struct BambuTagData {
  uint8_t  uid[4];
  char     uid_str[24];
  uint8_t  keys[16][6];
  uint8_t  blocks[64][16];
  bool     block_ok[64];
  char     tray_uuid[36];
  char     material[16];
  char     color_hex[8];
  char     vendor[32];
  char     detailed_filament[64];
  int      temp_min;
  int      temp_max;
  float    spool_weight;
  char     production_date[12];
  char     short_uid[20];
  bool     spoolman_found;
  int      spoolman_id;
  float    spoolman_remaining;
  float    spoolman_total;
  char     spoolman_last_dried[32];
};

enum TagType { TAG_BAMBU, TAG_SPOOLSCALE, TAG_BLANK, TAG_UNKNOWN };

struct UnlinkedSpool {
  int   id;
  char  name[48];
  char  vendor[32];
  char  material[16];
  char  color_hex[8];
  float remaining;
  float total;
  char  existing_tag[48];
  int   filament_id;
  float spool_weight;
};