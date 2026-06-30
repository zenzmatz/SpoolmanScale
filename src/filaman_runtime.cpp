#include "app_types.h"
#include "filaman_runtime.h"

#include <ArduinoJson.h>
#include <lvgl.h>

#include "filaman_http.h"
#include "lang.h"

extern BambuTagData g_tag;

extern bool sm_found;
extern int sm_id;
extern float sm_remaining;
extern float sm_total;
extern float sm_spool_weight;
extern char sm_last_dried[];
extern char sm_material_global[];
extern char sm_color_global[];
extern char sm_location_name[];
extern int sm_location_id;
extern char sm_last_used[];
extern int g_loc_popup_shown_for_id;

extern lv_obj_t *lbl_spoolman_weight;
extern lv_obj_t *lbl_spoolman_pct;
extern lv_obj_t *lbl_filament_name;
extern lv_obj_t *lbl_detail;
extern lv_obj_t *lbl_spoolman_id;
extern lv_obj_t *lbl_scale_diff;
extern lv_obj_t *lbl_spoolman_dried_val;
extern lv_obj_t *lbl_dried_sym;
extern lv_obj_t *lbl_last_used;
extern lv_obj_t *lbl_raw_info;
extern lv_obj_t *lbl_material;
extern lv_obj_t *lbl_vendor;
extern lv_obj_t *lbl_color_swatch;

void backendShowPendingStatus(const char* title, const char* detail);
void isoToDe(const char* iso, char* out, size_t len);
void driedDisplayStr(const char* de_date, char* out, size_t len);
void updateLinkButton();
void logSDf(const char* fmt, ...);

namespace {

void copyRuntimeString(char* dst, size_t len, const char* src) {
  if (!dst || len == 0) return;
  strncpy(dst, src ? src : "", len - 1);
  dst[len - 1] = '\0';
}

const char* filamanNotFoundText() {
  return g_lang == LANG_DE ? "Nicht in FilaMan" : "Not in FilaMan";
}

String filamanFetchLocationNameById(int location_id) {
  if (location_id <= 0) return String("");

  char path[48];
  snprintf(path, sizeof(path), "/api/v1/locations/%d", location_id);

  int http_code = 0;
  String response;
  if (!filamanGetAuthorized(path, http_code, response)) {
    return String("#") + String(location_id);
  }
  if (http_code != 200) {
    String detail = filamanErrorMessage(response, http_code);
    logSDf("FilaMan location fetch failed: id=%d HTTP %d %s",
      location_id, http_code, detail.c_str());
    return String("#") + String(location_id);
  }

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    logSDf("FilaMan location parse failed: id=%d", location_id);
    return String("#") + String(location_id);
  }

  const char* name = doc["name"] | "";
  return name[0] ? String(name) : (String("#") + String(location_id));
}

void applyFilamanLookupFailure(const char* feature, int http_code, const String& response) {
  sm_found = false;
  sm_id = 0;
  sm_remaining = 0;
  sm_total = 0;
  sm_spool_weight = 0;
  sm_last_dried[0] = '\0';
  sm_last_used[0] = '\0';
  sm_location_name[0] = '\0';
  sm_location_id = 0;

  String detail = filamanErrorMessage(response, http_code);
  Serial.printf("FilaMan %s failed: %d %s\n", feature, http_code, detail.c_str());
  logSDf("FilaMan %s failed: HTTP %d %s", feature, http_code, detail.c_str());

  if (lbl_spoolman_weight) {
    lv_label_set_text(lbl_spoolman_weight, http_code == 404 ? filamanNotFoundText() : "FilaMan err");
    lv_obj_set_style_text_color(lbl_spoolman_weight,
      lv_color_hex(http_code == 404 ? 0xf0b838 : 0xe04040), 0);
  }
  if (lbl_spoolman_pct) lv_label_set_text(lbl_spoolman_pct, "");
  if (lbl_filament_name) lv_label_set_text(lbl_filament_name, http_code == 404 ? "-" : detail.c_str());
  if (lbl_detail) lv_label_set_text(lbl_detail, http_code == 404 ? "-" : feature);
  if (lbl_spoolman_id) lv_label_set_text(lbl_spoolman_id, "?");
  if (lbl_scale_diff) lv_obj_set_width(lbl_scale_diff, 0);
  if (lbl_spoolman_dried_val) lv_label_set_text(lbl_spoolman_dried_val, "-");
  if (lbl_dried_sym) lv_obj_add_flag(lbl_dried_sym, LV_OBJ_FLAG_HIDDEN);
  if (lbl_last_used) lv_label_set_text(lbl_last_used, "-");
  if (lbl_raw_info) lv_label_set_text(lbl_raw_info, "");
  if (!g_tag.material[0]) {
    if (lbl_material) lv_label_set_text(lbl_material, "-");
    if (lbl_vendor) lv_label_set_text(lbl_vendor, "-");
    if (lbl_color_swatch) lv_obj_set_style_bg_color(lbl_color_swatch, lv_color_hex(0x333333), 0);
  }
  updateLinkButton();
}

bool applyFilamanSpoolObject(JsonObject spool) {
  int spool_id = spool["id"] | 0;
  float remaining_weight = spool["remaining_weight_g"] | -1.0f;
  if (spool_id <= 0 || remaining_weight < 0.0f) {
    return false;
  }

  float total_weight = spool["initial_total_weight_g"] | remaining_weight;
  if (total_weight < remaining_weight) total_weight = remaining_weight;
  float empty_spool_weight = spool["empty_spool_weight_g"] | 0.0f;
  int location_id = spool["location_id"] | 0;

  String filament_name = spool["filament"]["designation"] | String("");
  String vendor_name = spool["filament"]["manufacturer"]["name"] | String("");
  String material_name = spool["filament"]["material_type"] | String("");
  String material_subgroup = spool["filament"]["material_subgroup"] | String("");
  String color_name = spool["filament"]["manufacturer_color_name"] | String("");
  String last_used_iso = spool["last_used_at"] | String("");

  String location_name = "";
  if (location_id > 0) {
    if (sm_location_id == location_id && sm_location_name[0]) {
      location_name = String(sm_location_name);
    } else {
      location_name = filamanFetchLocationNameById(location_id);
    }
  }

  sm_found = true;
  sm_id = spool_id;
  sm_remaining = remaining_weight;
  sm_total = total_weight;
  sm_spool_weight = empty_spool_weight;
  sm_last_dried[0] = '\0';
  sm_location_id = location_id;
  copyRuntimeString(sm_location_name, 48, location_name.c_str());

  if (last_used_iso.length() >= 10) {
    char de_lu[12];
    isoToDe(last_used_iso.substring(0, 10).c_str(), de_lu, sizeof(de_lu));
    copyRuntimeString(sm_last_used, 32, de_lu);
  } else {
    copyRuntimeString(sm_last_used, 32, "-");
  }

  char id_buf[16];
  snprintf(id_buf, sizeof(id_buf), "%d", spool_id);
  if (lbl_spoolman_id) {
    lv_label_set_text(lbl_spoolman_id, id_buf);
    lv_obj_set_style_text_color(lbl_spoolman_id, lv_color_hex(0x28d49a), 0);
  }

  float pct = (sm_total > 0.0f) ? (sm_remaining / sm_total) * 100.0f : 0.0f;
  uint32_t pct_color;
  if (pct <= 10.0f)      pct_color = 0xe04040;
  else if (pct <= 30.0f) pct_color = 0xf0b838;
  else                   pct_color = 0x28d49a;

  char weight_buf[24];
  snprintf(weight_buf, sizeof(weight_buf), "%.0f g", remaining_weight);
  if (lbl_spoolman_weight) {
    lv_label_set_text(lbl_spoolman_weight, weight_buf);
    lv_obj_set_style_text_color(lbl_spoolman_weight, lv_color_hex(pct_color), 0);
  }
  if (lbl_spoolman_pct) {
    lv_label_set_text(lbl_spoolman_pct, sm_location_name[0] ? sm_location_name : "-");
    lv_obj_set_style_text_color(lbl_spoolman_pct, lv_color_hex(0x4a6fa0), 0);
  }
  if (lbl_scale_diff) {
    int bar_w = (int)((pct / 100.0f) * 190.0f);
    if (bar_w < 0) bar_w = 0;
    if (bar_w > 190) bar_w = 190;
    lv_obj_set_width(lbl_scale_diff, bar_w);
    lv_obj_set_style_bg_color(lbl_scale_diff, lv_color_hex(pct_color), 0);
  }

  if (lbl_filament_name) lv_label_set_text(lbl_filament_name, filament_name.length() ? filament_name.c_str() : "FilaMan spool");
  if (lbl_detail) lv_label_set_text(lbl_detail, color_name.length() ? color_name.c_str() : "-");
  if (lbl_last_used) {
    char last_used_display[48];
    driedDisplayStr(sm_last_used, last_used_display, sizeof(last_used_display));
    lv_label_set_text(lbl_last_used, last_used_display);
  }
  if (lbl_spoolman_dried_val) lv_label_set_text(lbl_spoolman_dried_val, "-");
  if (lbl_dried_sym) lv_obj_add_flag(lbl_dried_sym, LV_OBJ_FLAG_HIDDEN);
  if (lbl_color_swatch) lv_obj_set_style_bg_color(lbl_color_swatch, lv_color_hex(0x333333), 0);

  if (!g_tag.material[0]) {
    if (material_name.length() && material_subgroup.length()) {
      material_name += " ";
      material_name += material_subgroup;
    }
    if (lbl_material) lv_label_set_text(lbl_material, material_name.length() ? material_name.c_str() : "-");
    if (lbl_vendor) lv_label_set_text(lbl_vendor, vendor_name.length() ? vendor_name.c_str() : "-");
    copyRuntimeString(sm_material_global, 32, material_name.c_str());
    sm_color_global[0] = '\0';
  }

  updateLinkButton();
  return true;
}

bool applyFilamanWeightResponse(const String& response, float measured_weight_g) {
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    backendShowPendingStatus("FilaMan response", "Scale response could not be parsed.");
    return false;
  }

  int spool_id = doc["spool_id"] | 0;
  float remaining_weight = doc["remaining_weight_g"] | -1.0f;
  const char* filament_name = doc["filament_name"] | "";
  if (spool_id <= 0 || remaining_weight < 0.0f) {
    backendShowPendingStatus("FilaMan response", "Scale response was missing spool data.");
    return false;
  }

  float inferred_spool_weight = measured_weight_g - remaining_weight;
  if (inferred_spool_weight < 0.0f) inferred_spool_weight = 0.0f;
  bool same_spool = (sm_id == spool_id);

  sm_found = true;
  sm_id = spool_id;
  sm_remaining = remaining_weight;
  sm_total = 0.0f;
  sm_spool_weight = inferred_spool_weight;
  sm_last_dried[0] = '\0';
  sm_last_used[0] = '\0';
  if (!same_spool) {
    sm_location_name[0] = '\0';
    sm_location_id = 0;
  }

  char id_buf[16];
  snprintf(id_buf, sizeof(id_buf), "%d", spool_id);
  if (lbl_spoolman_id) {
    lv_label_set_text(lbl_spoolman_id, id_buf);
    lv_obj_set_style_text_color(lbl_spoolman_id, lv_color_hex(0x28d49a), 0);
  }

  char weight_buf[24];
  snprintf(weight_buf, sizeof(weight_buf), "%.0f g", remaining_weight);
  if (lbl_spoolman_weight) {
    lv_label_set_text(lbl_spoolman_weight, weight_buf);
    lv_obj_set_style_text_color(lbl_spoolman_weight, lv_color_hex(0x28d49a), 0);
  }
  if (lbl_spoolman_pct) {
    lv_label_set_text(lbl_spoolman_pct, sm_location_name[0] ? sm_location_name : "-");
    lv_obj_set_style_text_color(lbl_spoolman_pct, lv_color_hex(0x4a6fa0), 0);
  }
  if (lbl_scale_diff) lv_obj_set_width(lbl_scale_diff, 0);

  if (lbl_filament_name) lv_label_set_text(lbl_filament_name, filament_name[0] ? filament_name : "FilaMan spool");
  if (lbl_detail) lv_label_set_text(lbl_detail, "Device API");
  if (lbl_vendor) lv_label_set_text(lbl_vendor, "-");
  if (lbl_material && !g_tag.material[0]) lv_label_set_text(lbl_material, "-");
  if (lbl_last_used) lv_label_set_text(lbl_last_used, "-");
  if (lbl_spoolman_dried_val) lv_label_set_text(lbl_spoolman_dried_val, "-");
  if (lbl_dried_sym) lv_obj_add_flag(lbl_dried_sym, LV_OBJ_FLAG_HIDDEN);
  if (lbl_color_swatch) lv_obj_set_style_bg_color(lbl_color_swatch, lv_color_hex(0x333333), 0);

  updateLinkButton();
  return true;
}

}  // namespace

bool filamanLookupOrSyncWeight(const char* tag_uuid, int spool_id, float measured_weight_g, const char* context) {
  if ((!tag_uuid || !tag_uuid[0]) && spool_id <= 0) {
    backendShowPendingStatus(context, "No tag or spool ID is available.");
    return false;
  }

  if (measured_weight_g < 0.0f) measured_weight_g = 0.0f;

  JsonDocument doc;
  if (tag_uuid && tag_uuid[0]) doc["tag_uuid"] = tag_uuid;
  if (spool_id > 0) doc["spool_id"] = spool_id;
  doc["measured_weight_g"] = measured_weight_g;

  String payload;
  serializeJson(doc, payload);

  int http_code = 0;
  String response;
  if (!filamanPostAuthorized("/api/v1/devices/scale/weight", payload, http_code, response)) {
    return false;
  }

  if (http_code != 200) {
    applyFilamanLookupFailure(context, http_code, response);
    return false;
  }

  return applyFilamanWeightResponse(response, measured_weight_g);
}

bool filamanQuerySpoolById(int spool_id, const char* context) {
  if (spool_id <= 0) {
    backendShowPendingStatus(context, "No spool ID is available.");
    return false;
  }

  char path[48];
  snprintf(path, sizeof(path), "/api/v1/spools/%d", spool_id);

  int http_code = 0;
  String response;
  if (!filamanGetAuthorized(path, http_code, response)) {
    return false;
  }
  if (http_code != 200) {
    applyFilamanLookupFailure(context, http_code, response);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    backendShowPendingStatus(context, "Spool response could not be parsed.");
    return false;
  }

  if (!applyFilamanSpoolObject(doc.as<JsonObject>())) {
    backendShowPendingStatus(context, "Spool response was missing spool data.");
    return false;
  }

  return true;
}

bool filamanQuerySpoolByTag(const char* tag_uuid, const char* context) {
  if (!tag_uuid || !tag_uuid[0]) {
    backendShowPendingStatus(context, "No tag is available.");
    return false;
  }

  int page = 1;
  int total = 0;
  int scanned = 0;

  while (page <= 50) {
    char path[96];
    snprintf(path, sizeof(path), "/api/v1/spools?page=%d&page_size=100&include_archived=false", page);

    int http_code = 0;
    String response;
    if (!filamanGetAuthorized(path, http_code, response)) {
      return false;
    }
    if (http_code != 200) {
      applyFilamanLookupFailure(context, http_code, response);
      return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, response)) {
      backendShowPendingStatus(context, "Spool list response could not be parsed.");
      return false;
    }

    JsonArray items = doc["items"].as<JsonArray>();
    if (items.isNull()) {
      backendShowPendingStatus(context, "Spool list response was missing items.");
      return false;
    }

    total = doc["total"] | total;
    for (JsonObject spool : items) {
      ++scanned;
      String existing_tag = spool["rfid_uid"] | String("");
      existing_tag.trim();
      if (!existing_tag.length() || !existing_tag.equalsIgnoreCase(tag_uuid)) continue;
      return applyFilamanSpoolObject(spool);
    }

    if (items.size() == 0 || (total > 0 && scanned >= total)) break;
    ++page;
  }

  applyFilamanLookupFailure(context, 404, "");
  return false;
}

bool filamanLocateCurrentSpool(const char* location_value, String& status_detail) {
  status_detail = "";

  String location_ref = String(location_value ? location_value : "");
  location_ref.trim();
  if (!location_ref.length()) {
    status_detail = "Enter a location tag or ID first.";
    return false;
  }

  const char* spool_tag_uuid = g_tag.tray_uuid[0] ? g_tag.tray_uuid : (g_tag.uid_str[0] ? g_tag.uid_str : nullptr);
  if ((!spool_tag_uuid || !spool_tag_uuid[0]) && sm_id <= 0) {
    status_detail = "Scan a spool first.";
    return false;
  }

  bool is_numeric_location = true;
  for (size_t index = 0; index < location_ref.length(); ++index) {
    char ch = location_ref.charAt(index);
    if (ch < '0' || ch > '9') {
      is_numeric_location = false;
      break;
    }
  }

  JsonDocument doc;
  if (spool_tag_uuid && spool_tag_uuid[0]) doc["spool_tag_uuid"] = spool_tag_uuid;
  if (sm_id > 0) doc["spool_id"] = sm_id;

  int requested_location_id = 0;
  if (is_numeric_location) {
    requested_location_id = location_ref.toInt();
    if (requested_location_id <= 0) {
      status_detail = "Location ID must be positive.";
      return false;
    }
    doc["location_id"] = requested_location_id;
  } else {
    doc["location_tag_uuid"] = location_ref;
  }

  String payload;
  serializeJson(doc, payload);

  int http_code = 0;
  String response;
  if (!filamanPostAuthorized("/api/v1/devices/scale/locate", payload, http_code, response)) {
    status_detail = "Locate request could not be sent.";
    return false;
  }

  if (http_code != 200) {
    status_detail = filamanErrorMessage(response, http_code);
    backendShowPendingStatus("Location sync", status_detail.c_str());
    return false;
  }

  JsonDocument response_doc;
  if (deserializeJson(response_doc, response)) {
    status_detail = "Locate response could not be parsed.";
    backendShowPendingStatus("Location sync", status_detail.c_str());
    return false;
  }

  bool success = response_doc["success"].isNull() ? true : (bool)(response_doc["success"] | false);
  if (!success) {
    status_detail = response_doc["message"] | "Locate request failed.";
    backendShowPendingStatus("Location sync", status_detail.c_str());
    return false;
  }

  int response_location_id = response_doc["location_id"] | requested_location_id;
  const char* response_location_name = response_doc["location_name"] | "";

  sm_location_id = response_location_id;
  String display_name = response_location_name[0]
    ? String(response_location_name)
    : (is_numeric_location ? String("#") + String(sm_location_id) : location_ref);
  copyRuntimeString(sm_location_name, 48, display_name.c_str());
  g_loc_popup_shown_for_id = sm_id;

  if (lbl_detail) lv_label_set_text(lbl_detail, "Location sync");
  if (lbl_raw_info) lv_label_set_text(lbl_raw_info, display_name.c_str());

  status_detail = String("Located: ") + display_name;
  logSDf("FilaMan locate ok: spool_id=%d location_id=%d name=%s",
    sm_id, sm_location_id, display_name.c_str());
  return true;
}