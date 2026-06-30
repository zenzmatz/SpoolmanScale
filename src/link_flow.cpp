#include "link_flow.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "app_types.h"
#include "backend_types.h"
#include "bambu_blacklist.h"
#include "filaman_http.h"
#include "lang.h"

extern BambuTagData g_tag;

extern bool wifi_ok;
extern bool sd_verbose;
extern BackendMode cfg_backend_mode;
extern char cfg_spoolman_base[80];
extern char spoolman_queried_uid[24];

bool spoolmanPrepareRequest(HTTPClient& http);

extern UnlinkedSpool* link_spools;
extern int link_spool_count;
extern char link_tag_uid[24];
extern lv_obj_t* scr_link_list;
extern lv_obj_t* scr_link_entry;
extern lv_obj_t* scr_link_id;
extern lv_obj_t* scr_link_warn_a;
extern lv_obj_t* scr_link_warn_b;
extern lv_obj_t* scr_link_vendor;
extern lv_obj_t* scr_link_mat;
extern lv_obj_t* scr_link_mat_sub;
extern lv_obj_t* scr_link_spools;

extern char link_id_input[8];
extern lv_obj_t* lbl_link_id_display;
extern lv_obj_t* lbl_link_id_status;

extern char link_selected_vendor[32];
extern char link_selected_material[8];
extern char link_selected_material_full[32];
extern bool link_stage3_shown;
extern bool link_flow_is_bambu;

extern lv_obj_t* scr_copy_entry;
extern bool copy_flow_archived;
extern bool copy_flow_via_list;
extern bool copy_confirm_pending;
extern int copy_confirm_fid;
extern float copy_confirm_remaining;
extern float copy_confirm_initial;
extern float copy_confirm_spool_w;
extern char copy_confirm_name[80];

extern bool id_popup_is_bambu;
extern bool id_popup_is_copy;
extern int copy_id_lookup_pending;
extern int link_id_lookup_pending;
extern bool link_id_lookup_is_bambu;
extern bool show_id_input_pending;
extern bool show_id_input_rebuild;
extern bool id_input_open;

extern bool link_popup_dismissed;
extern int spool_list_limit;

void logSD(const char* msg);
void logSDf(const char* fmt, ...);
void backendShowPendingStatus(const char* title, const char* detail);
bool backendQueryCurrentSpoolById(int spool_id);
bool filamanLinkCurrentTagToSpool(int spool_id, String& status_detail);

namespace {

struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) ptr = malloc(size);
    return ptr;
  }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* ptr, size_t new_size) override {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

lv_obj_t* buildLinkOverlay() {
  lv_obj_t* scr = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr, 480, 320);
  lv_obj_set_pos(scr, 0, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

void doLinkPatch(int spool_id, bool is_bambu) {
  const char* link_uuid = is_bambu ? g_tag.tray_uuid : link_tag_uid;
  Serial.printf("doLinkPatch: ID=%d uuid=%s\n", spool_id, link_uuid);
  if (cfg_backend_mode == BACKEND_FILAMAN) {
    String status_detail;
    if (!filamanLinkCurrentTagToSpool(spool_id, status_detail)) {
      backendShowPendingStatus("Link spool", status_detail.length() ? status_detail.c_str() : "FilaMan link failed.");
      return;
    }
  } else {
    patchSpoolTag(spool_id, link_uuid);
  }

  if (scr_link_entry)  { lv_obj_del(scr_link_entry);  scr_link_entry = nullptr; }
  if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
  if (scr_link_warn_a) { lv_obj_del(scr_link_warn_a); scr_link_warn_a = nullptr; }
  if (scr_link_warn_b) { lv_obj_del(scr_link_warn_b); scr_link_warn_b = nullptr; }
  if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
  if (scr_link_mat)    { lv_obj_del(scr_link_mat);    scr_link_mat = nullptr; }
  if (scr_link_mat_sub){ lv_obj_del(scr_link_mat_sub);scr_link_mat_sub = nullptr; }
  if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }
  if (scr_link_list)   { lv_obj_del(scr_link_list);   scr_link_list = nullptr; }
  if (link_spools) { free(link_spools); link_spools = nullptr; }
  link_spool_count = 0;

  link_popup_dismissed = false;
  if (is_bambu) {
    spoolman_queried_uid[0] = '\0';
    backendQueryCurrentSpoolById(spool_id);
  } else {
    strncpy(g_tag.uid_str, link_tag_uid, sizeof(g_tag.uid_str) - 1);
    g_tag.uid_str[sizeof(g_tag.uid_str) - 1] = '\0';
    strncpy(g_tag.tray_uuid, link_tag_uid, sizeof(g_tag.tray_uuid) - 1);
    g_tag.tray_uuid[sizeof(g_tag.tray_uuid) - 1] = '\0';
    spoolman_queried_uid[0] = '\0';
    backendQueryCurrentSpoolById(spool_id);
  }
  Serial.printf("Linking complete! ID=%d\n", spool_id);
}

void showWarnPopupA(int spool_id, const char* existing_tag, bool is_bambu, const char* link_uuid) {
  (void)link_uuid;
  logSDf("SHOW: WarnPopupA spool=%d", spool_id);
  if (scr_link_warn_a) { lv_obj_del(scr_link_warn_a); scr_link_warn_a = nullptr; }

  scr_link_warn_a = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_link_warn_a, 480, 320);
  lv_obj_set_pos(scr_link_warn_a, 0, 0);
  lv_obj_set_style_bg_color(scr_link_warn_a, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr_link_warn_a, LV_OPA_80, 0);
  lv_obj_set_style_border_width(scr_link_warn_a, 0, 0);
  lv_obj_set_style_radius(scr_link_warn_a, 0, 0);
  lv_obj_set_style_pad_all(scr_link_warn_a, 0, 0);
  lv_obj_clear_flag(scr_link_warn_a, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(scr_link_warn_a);
  lv_obj_set_size(box, 440, 262);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x0c1828), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0xf0b838), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_title = lv_label_create(box);
  lv_label_set_text(lbl_title, T(STR_WARN_A_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xf0b838), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t* line = lv_obj_create(box);
  lv_obj_set_size(line, 420, 1);
  lv_obj_set_pos(line, 10, 42);
  lv_obj_set_style_bg_color(line, lv_color_hex(0x3a2800), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_radius(line, 0, 0);
  lv_obj_set_style_pad_all(line, 0, 0);

  char tag_short[14];
  snprintf(tag_short, sizeof(tag_short), "%.10s...", existing_tag);

  const char* sm_mat = "";
  const char* sm_name = "";
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id == spool_id) {
      sm_mat = link_spools[i].material;
      sm_name = link_spools[i].name;
      break;
    }
  }
  char info_buf[96];
  if (sm_mat[0] || sm_name[0]) {
    snprintf(info_buf, sizeof(info_buf), T(STR_WARN_A_SPOOL_INFO), spool_id, sm_mat, sm_name, tag_short);
  } else {
    snprintf(info_buf, sizeof(info_buf), T(STR_WARN_A_SPOOL_SHORT), spool_id, tag_short);
  }
  lv_obj_t* lbl_info = lv_label_create(box);
  lv_label_set_text(lbl_info, info_buf);
  lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_info, 400);
  lv_obj_align(lbl_info, LV_ALIGN_TOP_MID, 0, 54);

  static int warn_a_spool_id = 0;
  static bool warn_a_is_bambu = false;
  warn_a_spool_id = spool_id;
  warn_a_is_bambu = is_bambu;

  lv_obj_t* btn_force = lv_btn_create(box);
  lv_obj_set_size(btn_force, 420, 44);
  lv_obj_set_pos(btn_force, 10, 114);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(0x3a2800), 0);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(0x5a4000), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_force, 8, 0);
  lv_obj_set_style_shadow_width(btn_force, 0, 0);
  lv_obj_set_style_border_width(btn_force, 0, 0);
  lv_obj_add_event_cb(btn_force, [](lv_event_t* e) {
    (void)e;
    if (scr_link_warn_a) { lv_obj_del(scr_link_warn_a); scr_link_warn_a = nullptr; }
    if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
    doLinkPatch(warn_a_spool_id, warn_a_is_bambu);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_force = lv_label_create(btn_force);
  lv_label_set_text(lbl_force, T(STR_BTN_OVERWRITE));
  lv_obj_set_style_text_color(lbl_force, lv_color_hex(0xf0b838), 0);
  lv_obj_set_style_text_font(lbl_force, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_force);

  lv_obj_t* btn_retry = lv_btn_create(box);
  lv_obj_set_size(btn_retry, 420, 44);
  lv_obj_set_pos(btn_retry, 10, 166);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_retry, 8, 0);
  lv_obj_set_style_shadow_width(btn_retry, 0, 0);
  lv_obj_set_style_border_width(btn_retry, 1, 0);
  lv_obj_set_style_border_color(btn_retry, lv_color_hex(0x1a3060), 0);
  lv_obj_add_event_cb(btn_retry, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: WarnA -> retry IdInput (flag)");
    if (scr_link_warn_a) { lv_obj_del(scr_link_warn_a); scr_link_warn_a = nullptr; }
    if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
    link_id_input[0] = '\0';
    link_id_lookup_pending = 0;
    show_id_input_rebuild = true;
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_retry = lv_label_create(btn_retry);
  lv_label_set_text(lbl_retry, T(STR_ENTER_NEW_ID));
  lv_obj_set_style_text_color(lbl_retry, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_retry, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_retry);

  lv_obj_t* btn_cancel = lv_btn_create(box);
  lv_obj_set_size(btn_cancel, 420, 36);
  lv_obj_set_pos(btn_cancel, 10, 218);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_cancel, 0, 0);
  lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
    (void)e;
    if (scr_link_warn_a) { lv_obj_del(scr_link_warn_a); scr_link_warn_a = nullptr; }
    if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, T(STR_CANCEL));
  lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_ext_14, 0);
  lv_obj_center(lbl_cancel);
}

void showWarnPopupB(int spool_id, bool is_bambu) {
  logSDf("SHOW: WarnPopupB spool=%d", spool_id);
  if (scr_link_warn_b) { lv_obj_del(scr_link_warn_b); scr_link_warn_b = nullptr; }

  static int warn_b_spool_id = 0;
  static bool warn_b_is_bambu = false;
  warn_b_spool_id = spool_id;
  warn_b_is_bambu = is_bambu;

  scr_link_warn_b = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_link_warn_b, 480, 320);
  lv_obj_set_pos(scr_link_warn_b, 0, 0);
  lv_obj_set_style_bg_color(scr_link_warn_b, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr_link_warn_b, LV_OPA_80, 0);
  lv_obj_set_style_border_width(scr_link_warn_b, 0, 0);
  lv_obj_set_style_radius(scr_link_warn_b, 0, 0);
  lv_obj_set_style_pad_all(scr_link_warn_b, 0, 0);
  lv_obj_clear_flag(scr_link_warn_b, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(scr_link_warn_b);
  lv_obj_set_size(box, 440, 260);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x0c1828), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_title = lv_label_create(box);
  lv_label_set_text(lbl_title, T(STR_WARN_B_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t* line = lv_obj_create(box);
  lv_obj_set_size(line, 420, 1);
  lv_obj_set_pos(line, 10, 42);
  lv_obj_set_style_bg_color(line, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_radius(line, 0, 0);
  lv_obj_set_style_pad_all(line, 0, 0);

  char mat_buf[80];
  const char* sm_mat = "-";
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id == spool_id) { sm_mat = link_spools[i].material; break; }
  }
  snprintf(mat_buf, sizeof(mat_buf), T(STR_WARN_B_DETAILS), g_tag.material[0] ? g_tag.material : "?", sm_mat, spool_id);
  lv_obj_t* lbl_info = lv_label_create(box);
  lv_label_set_text(lbl_info, mat_buf);
  lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_info, 400);
  lv_obj_align(lbl_info, LV_ALIGN_TOP_MID, 0, 52);

  lv_obj_t* btn_force = lv_btn_create(box);
  lv_obj_set_size(btn_force, 420, 48);
  lv_obj_align(btn_force, LV_ALIGN_TOP_MID, 0, 142);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_force, 8, 0);
  lv_obj_set_style_shadow_width(btn_force, 0, 0);
  lv_obj_set_style_border_width(btn_force, 0, 0);
  lv_obj_add_event_cb(btn_force, [](lv_event_t* e) {
    (void)e;
    if (scr_link_warn_b) { lv_obj_del(scr_link_warn_b); scr_link_warn_b = nullptr; }
    if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
    doLinkPatch(warn_b_spool_id, warn_b_is_bambu);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_force = lv_label_create(btn_force);
  lv_label_set_text(lbl_force, T(STR_BTN_OVERWRITE));
  lv_obj_set_style_text_color(lbl_force, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(lbl_force, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_force);

  lv_obj_t* btn_retry = lv_btn_create(box);
  lv_obj_set_size(btn_retry, 420, 44);
  lv_obj_align(btn_retry, LV_ALIGN_TOP_MID, 0, 198);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_retry, 8, 0);
  lv_obj_set_style_shadow_width(btn_retry, 0, 0);
  lv_obj_set_style_border_width(btn_retry, 1, 0);
  lv_obj_set_style_border_color(btn_retry, lv_color_hex(0x1a3060), 0);
  lv_obj_add_event_cb(btn_retry, [](lv_event_t* e) {
    (void)e;
    if (scr_link_warn_b) { lv_obj_del(scr_link_warn_b); scr_link_warn_b = nullptr; }
    link_id_input[0] = '\0';
    showIdInputPopup(warn_b_is_bambu);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_retry = lv_label_create(btn_retry);
  lv_label_set_text(lbl_retry, T(STR_ENTER_NEW_ID));
  lv_obj_set_style_text_color(lbl_retry, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_retry, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_retry);

  lv_obj_t* btn_cancel = lv_btn_create(box);
  lv_obj_set_size(btn_cancel, 420, 36);
  lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x1a2030), 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x2a3040), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_cancel, 0, 0);
  lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
    (void)e;
    if (scr_link_warn_b) { lv_obj_del(scr_link_warn_b); scr_link_warn_b = nullptr; }
    if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, T(STR_CANCEL));
  lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_ext_14, 0);
  lv_obj_center(lbl_cancel);
}

void addListMoreInfoImpl(lv_obj_t* list, StringID str_id) {
  char buf[96];
  strncpy(buf, T(str_id), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  lv_obj_t* row = lv_obj_create(list);
  lv_obj_set_size(row, 452, 48);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x1a1a08), 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x3a3010), 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* lbl = lv_label_create(row);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xf0b838), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, 440);
  lv_obj_center(lbl);
}

}  // namespace

void addListMoreInfo(lv_obj_t* list, int str_id) {
  addListMoreInfoImpl(list, static_cast<StringID>(str_id));
}

bool extractBambuSubtype(const char* material, char* out_kw, size_t out_size) {
  if (!material || !material[0]) return false;
  const char* sep = nullptr;
  for (const char* p = material; *p; p++) {
    if (*p == '-' || *p == ' ') { sep = p + 1; break; }
  }
  if (!sep || !*sep) return false;
  strncpy(out_kw, sep, out_size - 1);
  out_kw[out_size - 1] = '\0';
  int len = strlen(out_kw);
  while (len > 0 && out_kw[len - 1] == ' ') out_kw[--len] = '\0';
  if (len == 0) return false;
  for (int i = 0; BAMBU_PLA_SUBTYPE_BLACKLIST[i]; i++) {
    if (strcasecmp(out_kw, BAMBU_PLA_SUBTYPE_BLACKLIST[i]) == 0) return true;
  }
  return false;
}

bool isSupportMaterial(const char* material_filter) {
  return material_filter && strncasecmp(material_filter, "Support", 7) == 0;
}

bool isSupportSpoolmanMat(const char* mat) {
  if (!mat) return false;
  size_t len = strlen(mat);
  return len >= 2 && mat[len - 2] == '-' && (mat[len - 1] == 'S' || mat[len - 1] == 's');
}

bool containsIgnoreCase(const char* haystack, const char* needle) {
  if (!haystack || !needle || !needle[0]) return false;
  size_t nlen = strlen(needle);
  size_t hlen = strlen(haystack);
  if (nlen > hlen) return false;
  for (size_t i = 0; i <= hlen - nlen; i++) {
    if (strncasecmp(haystack + i, needle, nlen) == 0) return true;
  }
  return false;
}

int colorDistance(const char* hex_a, const char* hex_b) {
  if (!hex_a || strlen(hex_a) < 7 || hex_a[0] != '#') return 999;
  if (!hex_b || strlen(hex_b) < 7 || hex_b[0] != '#') return 999;
  unsigned int r1, g1, b1, r2, g2, b2;
  if (sscanf(hex_a + 1, "%02X%02X%02X", &r1, &g1, &b1) != 3) return 999;
  if (sscanf(hex_b + 1, "%02X%02X%02X", &r2, &g2, &b2) != 3) return 999;
  return abs((int)r1 - (int)r2) + abs((int)g1 - (int)g2) + abs((int)b1 - (int)b2);
}

void fetchAllSpoolsForLink(bool is_bambu, const char* material_filter, bool archived_only) {
  if (link_spools) { free(link_spools); link_spools = nullptr; }
  link_spool_count = 0;
  if (!wifi_ok) return;

  if (cfg_backend_mode == BACKEND_FILAMAN) {
    if (archived_only) {
      logSD("FilaMan link fetch: archived list not supported for device flow");
      return;
    }

    const int page_size = 100;
    int total_in_api = -1;
    int page = 1;
    int skipped_tag = 0, skipped_vendor = 0, skipped_material = 0;
    int count_bambu = 0, count_linked = 0;

    while (true) {
      String path = String("/api/v1/spools?page=") + String(page) +
        "&page_size=" + String(page_size) + "&include_archived=false";
      int code = 0;
      String payload;
      if (!filamanGetAuthorized(path.c_str(), code, payload)) {
        if (link_spools) { free(link_spools); link_spools = nullptr; }
        link_spool_count = 0;
        return;
      }
      if (code != 200) {
        backendShowPendingStatus("FilaMan link list", filamanErrorMessage(payload, code).c_str());
        if (link_spools) { free(link_spools); link_spools = nullptr; }
        link_spool_count = 0;
        return;
      }

      StaticJsonDocument<512> filterL;
      JsonObject filterRoot = filterL.to<JsonObject>();
      filterRoot["total"] = true;
      JsonArray filterItems = filterRoot.createNestedArray("items");
      JsonObject fItem = filterItems.createNestedObject();
      fItem["id"] = true;
      fItem["filament_id"] = true;
      fItem["rfid_uid"] = true;
      fItem["remaining_weight_g"] = true;
      fItem["initial_total_weight_g"] = true;
      fItem["empty_spool_weight_g"] = true;
      fItem["filament"]["designation"] = true;
      fItem["filament"]["material_type"] = true;
      fItem["filament"]["material_subgroup"] = true;
      fItem["filament"]["manufacturer_color_name"] = true;
      fItem["filament"]["manufacturer"]["name"] = true;

      SpiRamAllocator psram_alloc;
      JsonDocument doc(&psram_alloc);
      if (deserializeJson(doc, payload, DeserializationOption::Filter(filterL))) {
        backendShowPendingStatus("FilaMan link list", "Spool list response could not be parsed.");
        if (link_spools) { free(link_spools); link_spools = nullptr; }
        link_spool_count = 0;
        return;
      }

      JsonArray items = doc["items"].as<JsonArray>();
      if (total_in_api < 0) {
        total_in_api = doc["total"] | (int)items.size();
        if (total_in_api > 0) {
          link_spools = (UnlinkedSpool*)heap_caps_malloc(total_in_api * sizeof(UnlinkedSpool), MALLOC_CAP_SPIRAM);
          if (!link_spools) link_spools = (UnlinkedSpool*)malloc(total_in_api * sizeof(UnlinkedSpool));
          if (!link_spools) {
            backendShowPendingStatus("FilaMan link list", "Not enough memory for the spool list.");
            link_spool_count = 0;
            return;
          }
        }
      }

      int items_on_page = 0;
      for (JsonObject spool : items) {
        items_on_page++;

        String existing_tag = spool["rfid_uid"] | String("");
        existing_tag.trim();
        if (existing_tag.length() > 0) { skipped_tag++; count_linked++; continue; }

        String vendor_name = spool["filament"]["manufacturer"]["name"] | String("");
        vendor_name.trim();
        bool bambu_vendor = (strncasecmp(vendor_name.c_str(), "Bambu", 5) == 0);
        if (bambu_vendor) count_bambu++;

        String material_name = spool["filament"]["material_type"] | String("");
        material_name.trim();
        if (is_bambu) {
          if (!bambu_vendor) { skipped_vendor++; continue; }
          if (material_filter && material_filter[0]) {
            if (isSupportMaterial(material_filter)) {
              if (!isSupportSpoolmanMat(material_name.c_str())) { skipped_material++; continue; }
            } else {
              if (strncasecmp(material_name.c_str(), material_filter, 3) != 0) { skipped_material++; continue; }
              if (isSupportSpoolmanMat(material_name.c_str())) { skipped_material++; continue; }
            }
          }
        }

        if (!link_spools || link_spool_count >= total_in_api) continue;
        UnlinkedSpool& s = link_spools[link_spool_count];
        memset(&s, 0, sizeof(s));
        s.id = spool["id"] | 0;
        s.filament_id = spool["filament_id"] | 0;
        s.remaining = spool["remaining_weight_g"] | 0.0f;
        s.total = spool["initial_total_weight_g"] | s.remaining;
        s.spool_weight = spool["empty_spool_weight_g"] | 0.0f;

        strncpy(s.existing_tag, existing_tag.c_str(), sizeof(s.existing_tag) - 1);

        String designation = spool["filament"]["designation"] | String("?");
        designation.trim();
        strncpy(s.name, designation.c_str(), sizeof(s.name) - 1);

        strncpy(s.vendor, vendor_name.c_str(), sizeof(s.vendor) - 1);

        String subgroup = spool["filament"]["material_subgroup"] | String("");
        subgroup.trim();
        if (subgroup.length() > 0 && subgroup.length() < (int)(sizeof(s.material) - material_name.length() - 2)) {
          material_name += "-";
          material_name += subgroup;
        }
        strncpy(s.material, material_name.c_str(), sizeof(s.material) - 1);

        String color_name = spool["filament"]["manufacturer_color_name"] | String("");
        color_name.trim();
        if (color_name.length() == 6 && color_name[0] != '#') color_name = "#" + color_name;
        strncpy(s.color_hex, color_name.c_str(), sizeof(s.color_hex) - 1);

        link_spool_count++;
      }

      if (items_on_page < page_size || link_spool_count >= total_in_api) break;
      page++;
    }

    logSDf("FilaMan inventory: %d total | %d linked | %d unlinked | %d Bambu",
      total_in_api < 0 ? 0 : total_in_api, count_linked, link_spool_count, count_bambu);
    Serial.printf("FilaMan inventory: %d total | %d linked | %d unlinked | %d Bambu\n",
      total_in_api < 0 ? 0 : total_in_api, count_linked, link_spool_count, count_bambu);
    logSDf("FilaMan link fetch: loaded=%d (skip_tag=%d skip_vendor=%d skip_mat=%d)",
      link_spool_count, skipped_tag, skipped_vendor, skipped_material);
    return;
  }

  logSDf("link fetch: is_bambu=%d material_filter='%s' archived_only=%d",
    is_bambu, material_filter ? material_filter : "", (int)archived_only);

  HTTPClient http;
  const char* url_suffix = archived_only ? "/api/v1/spool?allow_archived=true" : "/api/v1/spool?allow_archived=false";
  http.begin(String(cfg_spoolman_base) + url_suffix);
  spoolmanPrepareRequest(http);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return; }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<384> filterL;
  JsonArray filterL_arr = filterL.to<JsonArray>();
  JsonObject fL = filterL_arr.createNestedObject();
  fL["id"] = true;
  fL["archived"] = true;
  fL["remaining_weight"] = true;
  fL["extra"]["tag"] = true;
  fL["filament"]["id"] = true;
  fL["filament"]["name"] = true;
  fL["filament"]["material"] = true;
  fL["filament"]["weight"] = true;
  fL["filament"]["color_hex"] = true;
  fL["filament"]["vendor"]["name"] = true;
  fL["spool_weight"] = true;
  SpiRamAllocator psram_alloc;
  JsonDocument doc(&psram_alloc);
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filterL))) return;

  JsonArray spools = doc.as<JsonArray>();
  int total_in_api = 0;
  int skipped_tag = 0, skipped_vendor = 0, skipped_material = 0;
  int count_bambu = 0, count_linked = 0;

  int matched = 0;
  int skipped_archived = 0;
  for (JsonObject spool : spools) {
    total_in_api++;

    bool sp_archived = spool["archived"] | false;
    if (archived_only) {
      if (!sp_archived) { skipped_archived++; continue; }
    } else {
      if (sp_archived) { skipped_archived++; continue; }
    }

    String existing_tag = "";
    if (spool.containsKey("extra") && spool["extra"].containsKey("tag")) {
      existing_tag = spool["extra"]["tag"].as<String>();
      existing_tag.replace("\"", "");
      existing_tag.trim();
    }
    if (!archived_only && existing_tag.length() > 0) { skipped_tag++; count_linked++; continue; }

    String vname = "";
    if (spool["filament"].containsKey("vendor") && !spool["filament"]["vendor"].isNull()) {
      vname = spool["filament"]["vendor"]["name"] | String("");
    }
    vname.trim();
    bool bambu_vendor = (strncasecmp(vname.c_str(), "Bambu", 5) == 0);
    if (bambu_vendor) count_bambu++;

    if (is_bambu) {
      if (!bambu_vendor) { skipped_vendor++; continue; }
      if (material_filter && material_filter[0]) {
        String mat = spool["filament"]["material"] | String("");
        mat.trim();
        if (isSupportMaterial(material_filter)) {
          if (!isSupportSpoolmanMat(mat.c_str())) { skipped_material++; continue; }
        } else {
          if (strncasecmp(mat.c_str(), material_filter, 3) != 0) { skipped_material++; continue; }
          if (isSupportSpoolmanMat(mat.c_str())) { skipped_material++; continue; }
          char subkw[16];
          if (extractBambuSubtype(material_filter, subkw, sizeof(subkw))) {
            String fname = spool["filament"]["name"] | String("");
            if (!containsIgnoreCase(mat.c_str(), subkw) && !containsIgnoreCase(fname.c_str(), subkw)) {
              logSDf("link fetch: subtype skip mat='%s' name='%.20s' kw='%s'", mat.c_str(), fname.c_str(), subkw);
              skipped_material++;
              continue;
            }
          }
          if (g_tag.color_hex[0] == '#') {
            String col = spool["filament"]["color_hex"] | String("");
            char col_buf[8];
            snprintf(col_buf, sizeof(col_buf), "#%s", col.c_str());
            int dist = colorDistance(g_tag.color_hex, col_buf);
            if (dist > 120) { skipped_material++; continue; }
          }
        }
      }
    }
    matched++;
  }

  logSDf("Spoolman inventory: %d total | %d linked | %d unlinked | %d Bambu",
    total_in_api, count_linked, total_in_api - count_linked, count_bambu);
  Serial.printf("Spoolman inventory: %d total | %d linked | %d unlinked | %d Bambu\n",
    total_in_api, count_linked, total_in_api - count_linked, count_bambu);
  logSDf("link fetch: total=%d matched=%d (skip_tag=%d skip_vendor=%d skip_mat=%d)",
    total_in_api, matched, skipped_tag, skipped_vendor, skipped_material);
  Serial.printf("link fetch: total=%d matched=%d (skip_tag=%d skip_vendor=%d skip_mat=%d)\n",
    total_in_api, matched, skipped_tag, skipped_vendor, skipped_material);

  if (matched == 0) return;

  int alloc_count = matched;
  logSDf("link fetch: matched=%d, allocating all for vendor/material dedupe", matched);

  link_spools = (UnlinkedSpool*)heap_caps_malloc(alloc_count * sizeof(UnlinkedSpool), MALLOC_CAP_SPIRAM);
  if (!link_spools) {
    link_spools = (UnlinkedSpool*)malloc(alloc_count * sizeof(UnlinkedSpool));
    logSD("link fetch: PSRAM alloc failed, using internal RAM");
  }
  if (!link_spools) { logSD("link fetch: alloc failed completely"); return; }

  for (JsonObject spool : spools) {
    if (link_spool_count >= alloc_count) break;

    bool sp_archived = spool["archived"] | false;
    if (archived_only) {
      if (!sp_archived) continue;
    } else {
      if (sp_archived) continue;
    }

    String existing_tag = "";
    if (spool.containsKey("extra") && spool["extra"].containsKey("tag")) {
      existing_tag = spool["extra"]["tag"].as<String>();
      existing_tag.replace("\"", "");
      existing_tag.trim();
    }
    if (!archived_only && existing_tag.length() > 0) continue;

    String vname = "";
    if (spool["filament"].containsKey("vendor") && !spool["filament"]["vendor"].isNull()) {
      vname = spool["filament"]["vendor"]["name"] | String("");
    }
    vname.trim();
    bool bambu_vendor = (strncasecmp(vname.c_str(), "Bambu", 5) == 0);

    if (is_bambu) {
      if (!bambu_vendor) continue;
      if (material_filter && material_filter[0]) {
        String mat = spool["filament"]["material"] | String("");
        mat.trim();
        if (isSupportMaterial(material_filter)) {
          if (!isSupportSpoolmanMat(mat.c_str())) continue;
        } else {
          if (strncasecmp(mat.c_str(), material_filter, 3) != 0) continue;
          if (isSupportSpoolmanMat(mat.c_str())) continue;
          char subkw[16];
          if (extractBambuSubtype(material_filter, subkw, sizeof(subkw))) {
            String fname2 = spool["filament"]["name"] | String("");
            if (!containsIgnoreCase(mat.c_str(), subkw) && !containsIgnoreCase(fname2.c_str(), subkw)) continue;
          }
          if (g_tag.color_hex[0] == '#') {
            String col = spool["filament"]["color_hex"] | String("");
            char col_buf[8];
            snprintf(col_buf, sizeof(col_buf), "#%s", col.c_str());
            if (colorDistance(g_tag.color_hex, col_buf) > 120) continue;
          }
        }
      }
    }

    UnlinkedSpool& s = link_spools[link_spool_count];
    s.id = spool["id"] | 0;

    strncpy(s.existing_tag, existing_tag.c_str(), sizeof(s.existing_tag) - 1);
    s.existing_tag[sizeof(s.existing_tag) - 1] = '\0';

    String fname = spool["filament"]["name"] | String("?");
    fname.trim();
    strncpy(s.name, fname.c_str(), sizeof(s.name) - 1);
    s.name[sizeof(s.name) - 1] = '\0';

    strncpy(s.vendor, vname.c_str(), sizeof(s.vendor) - 1);
    s.vendor[sizeof(s.vendor) - 1] = '\0';

    String mat = spool["filament"]["material"] | String("");
    mat.trim();
    strncpy(s.material, mat.c_str(), sizeof(s.material) - 1);
    s.material[sizeof(s.material) - 1] = '\0';

    String col = spool["filament"]["color_hex"] | String("");
    col.trim();
    if (col.length() > 0 && col[0] != '#') col = "#" + col;
    strncpy(s.color_hex, col.c_str(), sizeof(s.color_hex) - 1);
    s.color_hex[sizeof(s.color_hex) - 1] = '\0';

    s.remaining = spool["remaining_weight"] | 0.0f;
    s.total = spool["filament"]["weight"] | 1000.0f;
    s.filament_id = spool["filament"]["id"] | 0;
    s.spool_weight = spool["spool_weight"] | 0.0f;

    if (sd_verbose) {
      logSDf("[verbose] link spool %d: vendor='%s' mat='%s' name='%s' fid=%d spw=%.0f",
        s.id, s.vendor, s.material, s.name, s.filament_id, s.spool_weight);
    }

    link_spool_count++;
  }
  Serial.printf("fetchAllSpoolsForLink: %d spools loaded (PSRAM)\n", link_spool_count);
  logSDf("link fetch done: %d spools in list", link_spool_count);
}

void fetchUnlinkedSpools() {
  fetchAllSpoolsForLink(false, "");
}

void patchSpoolTag(int spool_id, const char* uuid) {
  if (!wifi_ok) return;
  HTTPClient http;
  String url = String(cfg_spoolman_base) + "/api/v1/spool/" + spool_id;
  http.begin(url);
  spoolmanPrepareRequest(http);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  String body = "{\"extra\": {\"tag\": \"\\\"" + String(uuid) + "\\\"\"}}";
  Serial.printf("PATCH tag: %s -> %s\n", uuid, body.c_str());
  int code = http.PATCH(body);
  http.end();
  Serial.printf("patchSpoolTag: HTTP %d\n", code);
  logSDf("PATCH tag ID=%d HTTP %d", spool_id, code);
}

void linkIdLookupAndPatch(int entered_id, bool is_bambu) {
  if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_CHECKING));
  lv_timer_handler();
  if (!wifi_ok) {
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_NO_WIFI));
    return;
  }

  if (cfg_backend_mode == BACKEND_FILAMAN) {
    String status_detail;
    if (!filamanLinkCurrentTagToSpool(entered_id, status_detail)) {
      if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, status_detail.length() ? status_detail.c_str() : T(STR_ERR_SAVE));
      return;
    }

    if (scr_link_entry)  { lv_obj_del(scr_link_entry);  scr_link_entry = nullptr; }
    if (scr_link_id)     { lv_obj_del(scr_link_id);     scr_link_id = nullptr; }
    if (scr_link_warn_a) { lv_obj_del(scr_link_warn_a); scr_link_warn_a = nullptr; }
    if (scr_link_warn_b) { lv_obj_del(scr_link_warn_b); scr_link_warn_b = nullptr; }
    if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
    if (scr_link_mat)    { lv_obj_del(scr_link_mat);    scr_link_mat = nullptr; }
    if (scr_link_mat_sub){ lv_obj_del(scr_link_mat_sub);scr_link_mat_sub = nullptr; }
    if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }
    if (scr_link_list)   { lv_obj_del(scr_link_list);   scr_link_list = nullptr; }
    return;
  }

  HTTPClient http;
  String url = String(cfg_spoolman_base) + "/api/v1/spool/" + entered_id;
  http.begin(url);
  spoolmanPrepareRequest(http);
  http.setTimeout(5000);
  int code = http.GET();
  if (code == 404 || code < 0) {
    http.end();
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_ID_NOT_FOUND));
    return;
  }
  if (code != 200) {
    http.end();
    char err[32];
    snprintf(err, sizeof(err), T(STR_LINK_HTTP_ERR), code);
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, err);
    return;
  }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload)) {
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_JSON_ERR));
    return;
  }

  String existing = "";
  if (doc.containsKey("extra") && doc["extra"].containsKey("tag")) {
    existing = doc["extra"]["tag"].as<String>();
    existing.replace("\"", "");
    existing.trim();
  }

  if (link_spools == nullptr) {
    link_spools = (UnlinkedSpool*)heap_caps_malloc(sizeof(UnlinkedSpool), MALLOC_CAP_SPIRAM);
    if (!link_spools) link_spools = (UnlinkedSpool*)malloc(sizeof(UnlinkedSpool));
    link_spool_count = 0;
  }
  bool found_in_list = false;
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id == entered_id) { found_in_list = true; break; }
  }
  if (!found_in_list && link_spools != nullptr) {
    UnlinkedSpool& s = link_spools[link_spool_count];
    s.id = entered_id;
    strncpy(s.existing_tag, existing.c_str(), sizeof(s.existing_tag) - 1);
    String mat = doc["filament"]["material"] | String("");
    mat.trim();
    strncpy(s.material, mat.c_str(), sizeof(s.material) - 1);
    String fname = doc["filament"]["name"] | String("?");
    fname.trim();
    strncpy(s.name, fname.c_str(), sizeof(s.name) - 1);
    String vnd = doc["filament"]["vendor"]["name"] | String("");
    vnd.trim();
    strncpy(s.vendor, vnd.c_str(), sizeof(s.vendor) - 1);
    String col = doc["filament"]["color_hex"] | String("");
    col.trim();
    if (col.length() > 0 && col[0] != '#') col = "#" + col;
    strncpy(s.color_hex, col.c_str(), sizeof(s.color_hex) - 1);
    link_spool_count++;
  }

  if (existing.length() > 0) {
    showWarnPopupA(entered_id, existing.c_str(), is_bambu, "");
    return;
  }
  if (is_bambu && g_tag.material[0]) {
    String sm_mat = doc["filament"]["material"] | String("");
    sm_mat.trim();
    if (sm_mat.length() >= 3 && strlen(g_tag.material) >= 3) {
      if (strncasecmp(g_tag.material, sm_mat.c_str(), 3) != 0) {
        showWarnPopupB(entered_id, is_bambu);
        return;
      }
    }
  }
  doLinkPatch(entered_id, is_bambu);
}

void showIdInputPopup(bool is_bambu, bool is_copy) {
  logSDf("SHOW: IdInputPopup bambu=%d copy=%d", (int)is_bambu, (int)is_copy);
  id_input_open = true;
  if (scr_link_id) { lv_obj_del(scr_link_id); scr_link_id = nullptr; }
  lbl_link_id_display = nullptr;
  lbl_link_id_status = nullptr;

  scr_link_id = buildLinkOverlay();

  lv_obj_t* lbl_title = lv_label_create(scr_link_id);
  lv_label_set_text(lbl_title, T(STR_LINK_ID_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_18, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t* btn_back = lv_btn_create(scr_link_id);
  lv_obj_set_size(btn_back, 44, 44);
  lv_obj_set_pos(btn_back, 4, 2);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: IdInput -> Back (flag)");
    show_id_input_pending = false;
    if (id_popup_is_copy) {
      if (scr_link_id) { lv_obj_add_flag(scr_link_id, LV_OBJ_FLAG_HIDDEN); }
      if (scr_copy_entry) lv_obj_clear_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
      show_id_input_pending = true;
    } else {
      if (scr_link_id) { lv_obj_add_flag(scr_link_id, LV_OBJ_FLAG_HIDDEN); }
      if (scr_link_entry) lv_obj_clear_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
      show_id_input_pending = true;
    }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_bk = lv_label_create(btn_back);
  lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lbl_bk, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_bk);

  lv_obj_t* btn_x = lv_btn_create(scr_link_id);
  lv_obj_set_size(btn_x, 44, 44);
  lv_obj_align(btn_x, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_x, 8, 0);
  lv_obj_set_style_shadow_width(btn_x, 0, 0);
  lv_obj_set_style_border_width(btn_x, 0, 0);
  lv_obj_add_event_cb(btn_x, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: IdInput -> X Close");
    id_input_open = false;
    link_id_lookup_pending = 0;
    copy_id_lookup_pending = 0;
    show_id_input_pending = true;
    if (id_popup_is_copy) {
      if (scr_copy_entry) lv_obj_add_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
    } else {
      if (scr_link_entry) lv_obj_add_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
    }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_x = lv_label_create(btn_x);
  lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(lbl_x, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_x);

  lv_obj_t* div = lv_obj_create(scr_link_id);
  lv_obj_set_size(div, 472, 1);
  lv_obj_set_pos(div, 4, 48);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x1a3060), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t* lbl_ctx = lv_label_create(scr_link_id);
  char ctx_buf[48];
  if (is_bambu) {
    snprintf(ctx_buf, sizeof(ctx_buf), "Bambu  %s", g_tag.material[0] ? g_tag.material : "Tag");
  } else {
    snprintf(ctx_buf, sizeof(ctx_buf), "UID: %.14s", link_tag_uid);
  }
  lv_label_set_text(lbl_ctx, ctx_buf);
  lv_obj_set_style_text_color(lbl_ctx, lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_ctx, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_ctx, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_ctx, LV_ALIGN_TOP_MID, 0, 56);

  lv_obj_t* input_box = lv_obj_create(scr_link_id);
  lv_obj_set_size(input_box, 260, 44);
  lv_obj_align(input_box, LV_ALIGN_TOP_MID, 0, 76);
  lv_obj_set_style_bg_color(input_box, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_border_color(input_box, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_border_width(input_box, 1, 0);
  lv_obj_set_style_radius(input_box, 6, 0);
  lv_obj_set_style_pad_all(input_box, 0, 0);
  lv_obj_clear_flag(input_box, LV_OBJ_FLAG_SCROLLABLE);

  lbl_link_id_display = lv_label_create(input_box);
  lv_label_set_text(lbl_link_id_display, link_id_input[0] ? link_id_input : "_");
  lv_obj_set_style_text_color(lbl_link_id_display, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_link_id_display, &lv_font_montserrat_ext_24, 0);
  lv_obj_set_style_text_align(lbl_link_id_display, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl_link_id_display);

  lbl_link_id_status = lv_label_create(input_box);
  lv_label_set_text(lbl_link_id_status, "");
  lv_obj_set_style_text_color(lbl_link_id_status, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(lbl_link_id_status, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_align(lbl_link_id_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_link_id_status, LV_ALIGN_BOTTOM_MID, 0, -2);

  const int BTN_W = 80;
  const int BTN_H = 38;
  const int GAP = 5;
  const int PAD_X = (480 - 3 * BTN_W - 2 * GAP) / 2;
  const int START_Y = 132;

  const char* digits12[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK};
  int pos_x12[] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2};
  int pos_y12[] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};

  id_popup_is_bambu = is_bambu;
  id_popup_is_copy = is_copy;

  for (int d = 0; d < 12; d++) {
    lv_obj_t* btn = lv_btn_create(scr_link_id);
    int bx = PAD_X + pos_x12[d] * (BTN_W + GAP);
    int by = START_Y + pos_y12[d] * (BTN_H + GAP);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_pos(btn, bx, by);

    bool is_ok = (d == 11);
    bool is_backspace = (d == 10);
    uint32_t bg_col = is_ok ? 0x1a3020 : 0x0a1e30;
    uint32_t bg_pr = is_ok ? 0x2a5030 : 0x1a3060;
    uint32_t bd_col = is_ok ? 0x2a5030 : 0x1a3060;
    uint32_t tx_col = is_ok ? 0x40c080 : (is_backspace ? 0xf0b838 : 0xe8f0ff);

    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_col), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_pr), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(bd_col), 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, digits12[d]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(tx_col), 0);
    lv_obj_set_style_text_font(lbl, is_ok ? &lv_font_montserrat_ext_20 : &lv_font_montserrat_ext_18, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      const char* digit_str = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
      bool is_bs = (strcmp(digit_str, LV_SYMBOL_BACKSPACE) == 0);
      bool is_ok_btn = (strcmp(digit_str, LV_SYMBOL_OK) == 0);

      if (is_ok_btn) {
        if (strlen(link_id_input) == 0) {
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_ID_TITLE));
          return;
        }
        int entered_id = atoi(link_id_input);
        if (entered_id <= 0) {
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_ID_NOT_FOUND));
          return;
        }
        if (id_popup_is_copy) {
          if (!wifi_ok) {
            if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_NO_WIFI));
            return;
          }
          copy_id_lookup_pending = entered_id;
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_CHECKING));
        } else {
          link_id_lookup_pending = entered_id;
          link_id_lookup_is_bambu = id_popup_is_bambu;
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_CHECKING));
        }
      } else if (is_bs) {
        int len = strlen(link_id_input);
        if (len > 0) link_id_input[len - 1] = '\0';
        if (lbl_link_id_display) lv_label_set_text(lbl_link_id_display, link_id_input[0] ? link_id_input : "_");
        if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, "");
      } else {
        int len = strlen(link_id_input);
        if (len < 6) {
          link_id_input[len] = digit_str[0];
          link_id_input[len + 1] = '\0';
        }
        if (lbl_link_id_display) lv_label_set_text(lbl_link_id_display, link_id_input[0] ? link_id_input : "_");
        if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, "");
      }
    }, LV_EVENT_CLICKED, nullptr);
  }
}

void closeIdInputPopup() {
  id_input_open = false;
  link_id_lookup_pending = 0;
  copy_id_lookup_pending = 0;
  if (scr_link_id) { lv_obj_del(scr_link_id); scr_link_id = nullptr; }
  lbl_link_id_display = nullptr;
  lbl_link_id_status = nullptr;
}

void showFilteredSpoolList(const char* vendor_name, const char* material_prefix, const char* material_full) {
  logSDf("SHOW: FilteredSpoolList vendor=%s mat=%s matf=%s", vendor_name, material_prefix, material_full ? material_full : "");
  if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }

  scr_link_spools = buildLinkOverlay();

  int display_count = 0;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool& sc = link_spools[i];
    if (sc.existing_tag[0] != '\0' && !(copy_flow_via_list && copy_flow_archived)) continue;
    bool bv = (strncasecmp(sc.vendor, "Bambu", 5) == 0);
    if (link_flow_is_bambu) {
      if (!bv) continue;
      if (g_tag.material[0] && sc.material[0]) {
        if (isSupportMaterial(g_tag.material)) {
          if (!isSupportSpoolmanMat(sc.material)) continue;
        } else {
          if (material_prefix[0] && strncasecmp(sc.material, material_prefix, strlen(material_prefix)) != 0) continue;
          if (isSupportSpoolmanMat(sc.material)) continue;
        }
      }
    } else {
      if (bv) continue;
      if (vendor_name[0] && strncasecmp(sc.vendor, vendor_name, strlen(vendor_name)) != 0) continue;
      if (material_prefix[0] && strncasecmp(sc.material, material_prefix, strlen(material_prefix)) != 0) continue;
      if (material_full && material_full[0] && strcasecmp(sc.material, material_full) != 0) continue;
    }
    display_count++;
  }

  char title_buf[48];
  if (link_flow_is_bambu) {
    snprintf(title_buf, sizeof(title_buf), "Bambu %s - %d", g_tag.material[0] ? g_tag.material : "", display_count);
  } else if (material_full && material_full[0]) {
    snprintf(title_buf, sizeof(title_buf), "%.8s %.10s - %d", vendor_name, material_full, display_count);
  } else if (material_prefix[0]) {
    snprintf(title_buf, sizeof(title_buf), "%.8s %.4s - %d", vendor_name, material_prefix, display_count);
  } else {
    snprintf(title_buf, sizeof(title_buf), "%s - %d", T(STR_SPOOLS_ALL), display_count);
  }

  lv_obj_t* hdr = lv_obj_create(scr_link_spools);
  lv_obj_set_size(hdr, 480, 52);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_title = lv_label_create(hdr);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* btn_hdr_back = lv_btn_create(hdr);
  lv_obj_set_size(btn_hdr_back, 44, 44);
  lv_obj_set_pos(btn_hdr_back, 4, 4);
  lv_obj_set_style_bg_color(btn_hdr_back, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_hdr_back, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_hdr_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_hdr_back, 0, 0);
  lv_obj_set_style_border_width(btn_hdr_back, 0, 0);
  lv_obj_add_event_cb(btn_hdr_back, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: SpoolList -> Back");
    if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }
    if (link_flow_is_bambu) {
      if (scr_link_entry) lv_obj_clear_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
    } else {
      if (link_stage3_shown) {
        showMaterialSubList(link_selected_vendor, link_selected_material);
      } else {
        showMaterialList(link_selected_vendor);
      }
    }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* back_icon = lv_label_create(btn_hdr_back);
  lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_icon, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(back_icon);

  lv_obj_t* btn_hdr_cancel = lv_btn_create(hdr);
  lv_obj_set_size(btn_hdr_cancel, 44, 44);
  lv_obj_align(btn_hdr_cancel, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_hdr_cancel, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_hdr_cancel, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_hdr_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_hdr_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_hdr_cancel, 0, 0);
  lv_obj_add_event_cb(btn_hdr_cancel, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: SpoolList -> Cancel");
    if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }
    if (scr_link_entry)  { lv_obj_del(scr_link_entry);  scr_link_entry = nullptr; }
    if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
    if (scr_link_mat)    { lv_obj_del(scr_link_mat);    scr_link_mat = nullptr; }
    if (scr_link_mat_sub){ lv_obj_del(scr_link_mat_sub);scr_link_mat_sub = nullptr; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cancel_icon = lv_label_create(btn_hdr_cancel);
  lv_label_set_text(cancel_icon, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(cancel_icon, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(cancel_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(cancel_icon);

  lv_obj_t* div = lv_obj_create(scr_link_spools);
  lv_obj_set_size(div, 480, 1);
  lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x1a3060), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t* list = lv_obj_create(scr_link_spools);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  int count = 0;
  for (int i = 0; i < link_spool_count; i++) {
    if (count >= spool_list_limit) break;
    UnlinkedSpool& s = link_spools[i];

    if (s.existing_tag[0] != '\0' && !(copy_flow_via_list && copy_flow_archived)) continue;
    bool bambu_vendor = (strncasecmp(s.vendor, "Bambu", 5) == 0);
    if (link_flow_is_bambu) {
      if (!bambu_vendor) continue;
      if (g_tag.material[0] && s.material[0]) {
        if (isSupportMaterial(g_tag.material)) {
          if (!isSupportSpoolmanMat(s.material)) continue;
        } else {
          if (strncasecmp(s.material, g_tag.material, 3) != 0) continue;
          if (isSupportSpoolmanMat(s.material)) continue;
        }
      }
    } else {
      if (vendor_name[0] && strncasecmp(s.vendor, vendor_name, strlen(vendor_name)) != 0) continue;
      if (material_prefix[0] && strncasecmp(s.material, material_prefix, strlen(material_prefix)) != 0) continue;
      if (material_full && material_full[0] && strcasecmp(s.material, material_full) != 0) continue;
    }

    count++;
    lv_obj_t* row = lv_btn_create(list);
    lv_obj_set_size(row, 452, 56);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1a2840), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t* lbl_id = lv_label_create(row);
    char id_buf[10];
    snprintf(id_buf, sizeof(id_buf), "%d", s.id);
    lv_label_set_text(lbl_id, id_buf);
    lv_obj_set_style_text_color(lbl_id, lv_color_hex(0x28d49a), 0);
    lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_id, LV_ALIGN_TOP_LEFT, 6, 5);

    lv_obj_t* lbl_name = lv_label_create(row);
    char full_name[64];
    if (s.material[0]) {
      bool name_has_mat = (s.name[0] && strncasecmp(s.name, s.material, strlen(s.material)) == 0);
      if (name_has_mat) {
        strncpy(full_name, s.name, sizeof(full_name) - 1);
      } else {
        snprintf(full_name, sizeof(full_name), "%s %s", s.material, s.name);
      }
    } else {
      strncpy(full_name, s.name, sizeof(full_name) - 1);
    }
    full_name[sizeof(full_name) - 1] = '\0';
    lv_label_set_text(lbl_name, full_name);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 50, 5);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_name, 396);

    lv_obj_t* swatch = lv_obj_create(row);
    lv_obj_set_size(swatch, 14, 14);
    lv_obj_align(swatch, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_radius(swatch, 3, 0);
    lv_obj_set_style_border_width(swatch, 1, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(0x2a4060), 0);
    lv_obj_set_style_pad_all(swatch, 0, 0);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
    uint32_t swatch_col = 0x333333;
    if (s.color_hex[0] == '#' && strlen(s.color_hex) >= 7) {
      unsigned int r, g, b;
      sscanf(s.color_hex + 1, "%02X%02X%02X", &r, &g, &b);
      swatch_col = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    lv_obj_set_style_bg_color(swatch, lv_color_hex(swatch_col), 0);

    lv_obj_t* lbl_rest = lv_label_create(row);
    char rest_buf[24];
    if (s.remaining <= 0 && s.total > 0) {
      snprintf(rest_buf, sizeof(rest_buf), "%.0fg neu", s.total);
    } else {
      snprintf(rest_buf, sizeof(rest_buf), "%.0fg", s.remaining);
    }
    lv_label_set_text(lbl_rest, rest_buf);
    lv_obj_set_style_text_color(lbl_rest, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(lbl_rest, &lv_font_montserrat_ext_14, 0);
    lv_obj_align(lbl_rest, LV_ALIGN_BOTTOM_LEFT, 26, -5);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      if (idx < 0 || idx >= link_spool_count) return;
      UnlinkedSpool& s = link_spools[idx];

      lv_obj_t* popup = lv_obj_create(lv_scr_act());
      lv_obj_set_size(popup, 480, 320);
      lv_obj_set_pos(popup, 0, 0);
      lv_obj_set_style_bg_color(popup, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(popup, LV_OPA_70, 0);
      lv_obj_set_style_border_width(popup, 0, 0);
      lv_obj_set_style_radius(popup, 0, 0);
      lv_obj_set_style_pad_all(popup, 0, 0);
      lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t* box = lv_obj_create(popup);
      lv_obj_set_size(box, 440, 220);
      lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_bg_color(box, lv_color_hex(0x0c1828), 0);
      lv_obj_set_style_border_color(box, lv_color_hex(0x28d49a), 0);
      lv_obj_set_style_border_width(box, 2, 0);
      lv_obj_set_style_radius(box, 12, 0);
      lv_obj_set_style_pad_all(box, 0, 0);
      lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t* lbl_q = lv_label_create(box);
      lv_label_set_text(lbl_q, copy_flow_via_list ? T(STR_COPY_CONFIRM_TITLE) : T(STR_CONFIRM_LINK));
      lv_obj_set_style_text_color(lbl_q, lv_color_hex(0x28d49a), 0);
      lv_obj_set_style_text_font(lbl_q, &lv_font_montserrat_ext_18, 0);
      lv_obj_set_style_text_align(lbl_q, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(lbl_q, LV_ALIGN_TOP_MID, 0, 16);

      char info[80];
      bool name_has_mat = (s.material[0] && s.name[0] && strncasecmp(s.name, s.material, strlen(s.material)) == 0);
      if (name_has_mat) {
        snprintf(info, sizeof(info), "#%d  %s\n%.0fg / %.0fg", s.id, s.name, s.remaining, s.total);
      } else {
        snprintf(info, sizeof(info), "#%d  %s %s\n%.0fg / %.0fg", s.id, s.material, s.name, s.remaining, s.total);
      }
      lv_obj_t* lbl_info = lv_label_create(box);
      lv_label_set_text(lbl_info, info);
      lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xc8d8f0), 0);
      lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_ext_16, 0);
      lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(lbl_info, 400);
      lv_obj_align(lbl_info, LV_ALIGN_TOP_MID, 0, 48);

      lv_obj_t* btn_yes = lv_btn_create(box);
      lv_obj_set_size(btn_yes, 420, 46);
      lv_obj_set_pos(btn_yes, 10, 110);
      lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0x1a3020), 0);
      lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0x2a5030), LV_STATE_PRESSED);
      lv_obj_set_style_radius(btn_yes, 8, 0);
      lv_obj_set_style_shadow_width(btn_yes, 0, 0);
      lv_obj_set_style_border_width(btn_yes, 0, 0);
      lv_obj_set_user_data(btn_yes, (void*)(intptr_t)idx);
      lv_obj_add_event_cb(btn_yes, [](lv_event_t* e) {
        int cidx = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        lv_obj_t* pop = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e)));
        lv_obj_del(pop);
        if (copy_flow_via_list) {
          copy_flow_via_list = false;
          UnlinkedSpool& cs = link_spools[cidx];
          logSDf("CopyConfirm via list: spool_id=%d fid=%d spw=%.0f", cs.id, cs.filament_id, cs.spool_weight);
          copy_confirm_fid = cs.filament_id;
          copy_confirm_remaining = cs.remaining;
          copy_confirm_initial = cs.total;
          copy_confirm_spool_w = cs.spool_weight;
          bool nm = (cs.material[0] && cs.name[0] && strncasecmp(cs.name, cs.material, strlen(cs.material)) == 0);
          if (nm) {
            snprintf(copy_confirm_name, sizeof(copy_confirm_name), "%s (%s)", cs.name, cs.vendor);
          } else {
            snprintf(copy_confirm_name, sizeof(copy_confirm_name), "%s %s (%s)", cs.material, cs.name, cs.vendor);
          }
          copy_confirm_pending = true;
        } else {
          doLinkPatch(link_spools[cidx].id, link_flow_is_bambu);
        }
      }, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* lbl_yes = lv_label_create(btn_yes);
      lv_label_set_text(lbl_yes, copy_flow_via_list ? T(STR_BTN_CONFIRMED) : T(STR_LINK_OK));
      lv_obj_set_style_text_color(lbl_yes, lv_color_hex(0x40c080), 0);
      lv_obj_set_style_text_font(lbl_yes, &lv_font_montserrat_ext_18, 0);
      lv_obj_center(lbl_yes);

      lv_obj_t* btn_no = lv_btn_create(box);
      lv_obj_set_size(btn_no, 420, 40);
      lv_obj_set_pos(btn_no, 10, 164);
      lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x3a1010), 0);
      lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x602020), LV_STATE_PRESSED);
      lv_obj_set_style_radius(btn_no, 8, 0);
      lv_obj_set_style_shadow_width(btn_no, 0, 0);
      lv_obj_set_style_border_width(btn_no, 0, 0);
      lv_obj_add_event_cb(btn_no, [](lv_event_t* e) {
        lv_obj_del(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
      }, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* lbl_no = lv_label_create(btn_no);
      lv_label_set_text(lbl_no, T(STR_CANCEL));
      lv_obj_set_style_text_color(lbl_no, lv_color_hex(0xff8080), 0);
      lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_ext_14, 0);
      lv_obj_center(lbl_no);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  if (count == 0) {
    lv_obj_t* lbl_empty = lv_label_create(scr_link_spools);
    lv_label_set_text(lbl_empty, T(STR_NO_SPOOLS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(0xf0b838), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (count >= spool_list_limit) {
    addListMoreInfo(list, STR_LIST_MORE_SPOOLS);
  }
}

void showMaterialList(const char* vendor_name) {
  logSDf("SHOW: MaterialList vendor=%s", vendor_name);
  if (scr_link_mat) { lv_obj_del(scr_link_mat); scr_link_mat = nullptr; }
  strncpy(link_selected_vendor, vendor_name, sizeof(link_selected_vendor) - 1);
  link_selected_vendor[sizeof(link_selected_vendor) - 1] = '\0';
  link_selected_material_full[0] = 0;
  link_stage3_shown = false;

  scr_link_mat = buildLinkOverlay();

  char title_buf[48];
  snprintf(title_buf, sizeof(title_buf), "%s | %.16s", T(STR_MAT_TITLE), vendor_name);

  lv_obj_t* hdr_mat = lv_obj_create(scr_link_mat);
  lv_obj_set_size(hdr_mat, 480, 52);
  lv_obj_set_pos(hdr_mat, 0, 0);
  lv_obj_set_style_bg_color(hdr_mat, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(hdr_mat, 0, 0);
  lv_obj_set_style_pad_all(hdr_mat, 0, 0);
  lv_obj_set_style_radius(hdr_mat, 0, 0);
  lv_obj_clear_flag(hdr_mat, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl_title = lv_label_create(hdr_mat);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);
  lv_obj_t* btn_mat_back = lv_btn_create(hdr_mat);
  lv_obj_set_size(btn_mat_back, 44, 44);
  lv_obj_set_pos(btn_mat_back, 4, 4);
  lv_obj_set_style_bg_color(btn_mat_back, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_mat_back, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_mat_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_mat_back, 0, 0);
  lv_obj_set_style_border_width(btn_mat_back, 0, 0);
  lv_obj_add_event_cb(btn_mat_back, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: MatList -> Back");
    if (scr_link_mat) { lv_obj_del(scr_link_mat); scr_link_mat = nullptr; }
    showVendorList();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* mat_back_icon = lv_label_create(btn_mat_back);
  lv_label_set_text(mat_back_icon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(mat_back_icon, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(mat_back_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(mat_back_icon);
  lv_obj_t* btn_mat_cancel = lv_btn_create(hdr_mat);
  lv_obj_set_size(btn_mat_cancel, 44, 44);
  lv_obj_align(btn_mat_cancel, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_mat_cancel, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_mat_cancel, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_mat_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_mat_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_mat_cancel, 0, 0);
  lv_obj_add_event_cb(btn_mat_cancel, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: MatList -> Cancel");
    copy_flow_via_list = false;
    if (scr_link_mat)    { lv_obj_del(scr_link_mat);    scr_link_mat = nullptr; }
    if (scr_link_mat_sub){ lv_obj_del(scr_link_mat_sub);scr_link_mat_sub = nullptr; }
    if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
    if (scr_link_entry)  { lv_obj_del(scr_link_entry);  scr_link_entry = nullptr; }
    if (scr_copy_entry)  { lv_obj_del(scr_copy_entry);  scr_copy_entry = nullptr; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* mat_cancel_icon = lv_label_create(btn_mat_cancel);
  lv_label_set_text(mat_cancel_icon, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(mat_cancel_icon, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(mat_cancel_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(mat_cancel_icon);
  lv_obj_t* div = lv_obj_create(scr_link_mat);
  lv_obj_set_size(div, 480, 1);
  lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x1a3060), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t* list = lv_obj_create(scr_link_mat);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  static char seen_mats[20][4] = {};
  static int mat_counts[20] = {};
  static int seen_count = 0;
  seen_count = 0;
  memset(seen_mats, 0, sizeof(seen_mats));
  memset(mat_counts, 0, sizeof(mat_counts));

  bool mat_limit_hit = false;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool& s = link_spools[i];
    if (s.existing_tag[0] != '\0' && !(copy_flow_via_list && copy_flow_archived)) continue;
    if (strncasecmp(s.vendor, vendor_name, strlen(vendor_name)) != 0) continue;
    if (!s.material[0]) continue;
    char prefix[4];
    strncpy(prefix, s.material, 3);
    prefix[3] = '\0';
    bool found = false;
    for (int j = 0; j < seen_count; j++) {
      if (strncasecmp(seen_mats[j], prefix, 3) == 0) { mat_counts[j]++; found = true; break; }
    }
    if (!found) {
      if (seen_count >= spool_list_limit) { mat_limit_hit = true; continue; }
      strncpy(seen_mats[seen_count], prefix, 3);
      mat_counts[seen_count] = 1;
      seen_count++;
    }
  }

  for (int m = 0; m < seen_count; m++) {
    lv_obj_t* row = lv_btn_create(list);
    lv_obj_set_size(row, 452, 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1a2840), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t* lbl_mat = lv_label_create(row);
    lv_label_set_text(lbl_mat, seen_mats[m]);
    lv_obj_set_style_text_color(lbl_mat, lv_color_hex(0xf0b838), 0);
    lv_obj_set_style_text_font(lbl_mat, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_mat, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t* lbl_cnt = lv_label_create(row);
    char cnt_buf[12];
    snprintf(cnt_buf, sizeof(cnt_buf), "%d x", mat_counts[m]);
    lv_label_set_text(lbl_cnt, cnt_buf);
    lv_obj_set_style_text_color(lbl_cnt, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      strncpy(link_selected_material, seen_mats[idx], sizeof(link_selected_material) - 1);
      link_selected_material[sizeof(link_selected_material) - 1] = '\0';
      link_selected_material_full[0] = 0;
      if (scr_link_mat) { lv_obj_del(scr_link_mat); scr_link_mat = nullptr; }
      showMaterialSubList(link_selected_vendor, link_selected_material);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)m);
  }

  if (seen_count == 0) {
    lv_obj_t* lbl_empty = lv_label_create(scr_link_mat);
    lv_label_set_text(lbl_empty, T(STR_NO_MATERIALS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(0xf0b838), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (mat_limit_hit) {
    addListMoreInfo(list, STR_LIST_MORE_MATS);
  }
}

void showMaterialSubList(const char* vendor_name, const char* material_prefix) {
  logSDf("SHOW: MaterialSubList vendor=%s mat=%s", vendor_name, material_prefix);

  static char seen_full[20][32] = {};
  static int full_counts[20] = {};
  static int full_seen_count = 0;
  full_seen_count = 0;
  memset(seen_full, 0, sizeof(seen_full));
  memset(full_counts, 0, sizeof(full_counts));

  bool full_limit_hit = false;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool& s = link_spools[i];
    if (s.existing_tag[0] != '\0' && !(copy_flow_via_list && copy_flow_archived)) continue;
    if (strncasecmp(s.vendor, vendor_name, strlen(vendor_name)) != 0) continue;
    if (!s.material[0]) continue;
    if (strncasecmp(s.material, material_prefix, strlen(material_prefix)) != 0) continue;

    bool found = false;
    for (int j = 0; j < full_seen_count; j++) {
      if (strcasecmp(seen_full[j], s.material) == 0) { full_counts[j]++; found = true; break; }
    }
    if (!found) {
      if (full_seen_count >= spool_list_limit) { full_limit_hit = true; continue; }
      strncpy(seen_full[full_seen_count], s.material, sizeof(seen_full[0]) - 1);
      full_counts[full_seen_count] = 1;
      full_seen_count++;
    }
  }

  if (full_seen_count == 1 && !full_limit_hit) {
    logSDf("MaterialSubList auto-skip: only %s", seen_full[0]);
    strncpy(link_selected_material_full, seen_full[0], sizeof(link_selected_material_full) - 1);
    link_selected_material_full[sizeof(link_selected_material_full) - 1] = '\0';
    link_stage3_shown = false;
    showFilteredSpoolList(vendor_name, material_prefix, link_selected_material_full);
    return;
  }

  link_stage3_shown = true;
  if (scr_link_mat_sub) { lv_obj_del(scr_link_mat_sub); scr_link_mat_sub = nullptr; }
  scr_link_mat_sub = buildLinkOverlay();

  char title_buf[48];
  snprintf(title_buf, sizeof(title_buf), "%.16s | %.4s", vendor_name, material_prefix);

  lv_obj_t* hdr_ms = lv_obj_create(scr_link_mat_sub);
  lv_obj_set_size(hdr_ms, 480, 52);
  lv_obj_set_pos(hdr_ms, 0, 0);
  lv_obj_set_style_bg_color(hdr_ms, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(hdr_ms, 0, 0);
  lv_obj_set_style_pad_all(hdr_ms, 0, 0);
  lv_obj_set_style_radius(hdr_ms, 0, 0);
  lv_obj_clear_flag(hdr_ms, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl_title = lv_label_create(hdr_ms);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* btn_ms_back = lv_btn_create(hdr_ms);
  lv_obj_set_size(btn_ms_back, 44, 44);
  lv_obj_set_pos(btn_ms_back, 4, 4);
  lv_obj_set_style_bg_color(btn_ms_back, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_ms_back, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ms_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_ms_back, 0, 0);
  lv_obj_set_style_border_width(btn_ms_back, 0, 0);
  lv_obj_add_event_cb(btn_ms_back, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: MatSubList -> Back");
    if (scr_link_mat_sub) { lv_obj_del(scr_link_mat_sub); scr_link_mat_sub = nullptr; }
    showMaterialList(link_selected_vendor);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ms_back_icon = lv_label_create(btn_ms_back);
  lv_label_set_text(ms_back_icon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(ms_back_icon, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(ms_back_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(ms_back_icon);

  lv_obj_t* btn_ms_cancel = lv_btn_create(hdr_ms);
  lv_obj_set_size(btn_ms_cancel, 44, 44);
  lv_obj_align(btn_ms_cancel, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_ms_cancel, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_ms_cancel, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ms_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_ms_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_ms_cancel, 0, 0);
  lv_obj_add_event_cb(btn_ms_cancel, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: MatSubList -> Cancel");
    copy_flow_via_list = false;
    if (scr_link_mat_sub){ lv_obj_del(scr_link_mat_sub);scr_link_mat_sub = nullptr; }
    if (scr_link_mat)    { lv_obj_del(scr_link_mat);    scr_link_mat = nullptr; }
    if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
    if (scr_link_entry)  { lv_obj_del(scr_link_entry);  scr_link_entry = nullptr; }
    if (scr_copy_entry)  { lv_obj_del(scr_copy_entry);  scr_copy_entry = nullptr; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ms_cancel_icon = lv_label_create(btn_ms_cancel);
  lv_label_set_text(ms_cancel_icon, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(ms_cancel_icon, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(ms_cancel_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(ms_cancel_icon);

  lv_obj_t* div = lv_obj_create(scr_link_mat_sub);
  lv_obj_set_size(div, 480, 1);
  lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x1a3060), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t* list = lv_obj_create(scr_link_mat_sub);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  for (int m = 0; m < full_seen_count; m++) {
    lv_obj_t* row = lv_btn_create(list);
    lv_obj_set_size(row, 452, 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1a2840), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t* lbl_full = lv_label_create(row);
    lv_label_set_text(lbl_full, seen_full[m]);
    lv_obj_set_style_text_color(lbl_full, lv_color_hex(0xf0b838), 0);
    lv_obj_set_style_text_font(lbl_full, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_full, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_long_mode(lbl_full, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_full, 340);

    lv_obj_t* lbl_cnt = lv_label_create(row);
    char cnt_buf[12];
    snprintf(cnt_buf, sizeof(cnt_buf), "%d x", full_counts[m]);
    lv_label_set_text(lbl_cnt, cnt_buf);
    lv_obj_set_style_text_color(lbl_cnt, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      strncpy(link_selected_material_full, seen_full[idx], sizeof(link_selected_material_full) - 1);
      link_selected_material_full[sizeof(link_selected_material_full) - 1] = '\0';
      if (scr_link_mat_sub) { lv_obj_del(scr_link_mat_sub); scr_link_mat_sub = nullptr; }
      showFilteredSpoolList(link_selected_vendor, link_selected_material, link_selected_material_full);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)m);
  }

  if (full_seen_count == 0) {
    lv_obj_t* lbl_empty = lv_label_create(scr_link_mat_sub);
    lv_label_set_text(lbl_empty, T(STR_NO_MATERIALS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(0xf0b838), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (full_limit_hit) {
    addListMoreInfo(list, STR_LIST_MORE_MATS);
  }
}

void showVendorList() {
  logSD("SHOW: VendorList");
  if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }

  scr_link_vendor = buildLinkOverlay();

  int total_unlinked = 0;
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].existing_tag[0] != '\0' && !(copy_flow_via_list && copy_flow_archived)) continue;
    total_unlinked++;
  }

  char title_buf[40];
  snprintf(title_buf, sizeof(title_buf), T(STR_VENDOR_TITLE), total_unlinked);

  lv_obj_t* hdr_vnd = lv_obj_create(scr_link_vendor);
  lv_obj_set_size(hdr_vnd, 480, 52);
  lv_obj_set_pos(hdr_vnd, 0, 0);
  lv_obj_set_style_bg_color(hdr_vnd, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(hdr_vnd, 0, 0);
  lv_obj_set_style_pad_all(hdr_vnd, 0, 0);
  lv_obj_set_style_radius(hdr_vnd, 0, 0);
  lv_obj_clear_flag(hdr_vnd, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl_title = lv_label_create(hdr_vnd);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* btn_vnd_back = lv_btn_create(hdr_vnd);
  lv_obj_set_size(btn_vnd_back, 44, 44);
  lv_obj_set_pos(btn_vnd_back, 4, 4);
  lv_obj_set_style_bg_color(btn_vnd_back, lv_color_hex(0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_vnd_back, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_vnd_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_vnd_back, 0, 0);
  lv_obj_set_style_border_width(btn_vnd_back, 0, 0);
  lv_obj_add_event_cb(btn_vnd_back, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: VendorList -> Back");
    if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
    if (scr_link_entry) lv_obj_clear_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
    if (scr_copy_entry) lv_obj_clear_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* vnd_back_icon = lv_label_create(btn_vnd_back);
  lv_label_set_text(vnd_back_icon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(vnd_back_icon, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(vnd_back_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(vnd_back_icon);

  lv_obj_t* btn_vnd_x = lv_btn_create(hdr_vnd);
  lv_obj_set_size(btn_vnd_x, 44, 44);
  lv_obj_align(btn_vnd_x, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_vnd_x, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_vnd_x, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_vnd_x, 8, 0);
  lv_obj_set_style_shadow_width(btn_vnd_x, 0, 0);
  lv_obj_set_style_border_width(btn_vnd_x, 0, 0);
  lv_obj_add_event_cb(btn_vnd_x, [](lv_event_t* e) {
    (void)e;
    logSD("BTN: VendorList -> Cancel");
    copy_flow_via_list = false;
    if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
    if (scr_link_entry)  { lv_obj_del(scr_link_entry);  scr_link_entry = nullptr; }
    if (scr_copy_entry)  { lv_obj_del(scr_copy_entry);  scr_copy_entry = nullptr; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* vnd_cancel_icon = lv_label_create(btn_vnd_x);
  lv_label_set_text(vnd_cancel_icon, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(vnd_cancel_icon, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(vnd_cancel_icon, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(vnd_cancel_icon);
  lv_obj_t* div = lv_obj_create(scr_link_vendor);
  lv_obj_set_size(div, 480, 1);
  lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x1a3060), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t* list = lv_obj_create(scr_link_vendor);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  static char seen_vendors[20][32] = {};
  static int vendor_counts[20] = {};
  static int seen_v = 0;
  seen_v = 0;
  memset(seen_vendors, 0, sizeof(seen_vendors));
  memset(vendor_counts, 0, sizeof(vendor_counts));

  bool vendor_limit_hit = false;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool& s = link_spools[i];
    if (s.existing_tag[0] != '\0' && !(copy_flow_via_list && copy_flow_archived)) continue;
    const char* vn = s.vendor[0] ? s.vendor : "Unbekannt";
    bool found = false;
    for (int j = 0; j < seen_v; j++) {
      if (strcasecmp(seen_vendors[j], vn) == 0) { vendor_counts[j]++; found = true; break; }
    }
    if (!found) {
      if (seen_v >= spool_list_limit) { vendor_limit_hit = true; continue; }
      strncpy(seen_vendors[seen_v], vn, 31);
      vendor_counts[seen_v] = 1;
      seen_v++;
    }
  }

  for (int v = 0; v < seen_v; v++) {
    lv_obj_t* row = lv_btn_create(list);
    lv_obj_set_size(row, 452, 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a3060), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1a2840), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t* lbl_vnd = lv_label_create(row);
    lv_label_set_text(lbl_vnd, seen_vendors[v]);
    lv_obj_set_style_text_color(lbl_vnd, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl_vnd, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_vnd, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_long_mode(lbl_vnd, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_vnd, 320);

    lv_obj_t* lbl_cnt = lv_label_create(row);
    char cnt_buf[12];
    snprintf(cnt_buf, sizeof(cnt_buf), "%d x", vendor_counts[v]);
    lv_label_set_text(lbl_cnt, cnt_buf);
    lv_obj_set_style_text_color(lbl_cnt, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
      showMaterialList(seen_vendors[idx]);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)v);
  }

  if (seen_v == 0) {
    lv_obj_t* lbl_empty = lv_label_create(scr_link_vendor);
    lv_label_set_text(lbl_empty, T(STR_NO_VENDORS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(0xf0b838), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (vendor_limit_hit) {
    addListMoreInfo(list, STR_LIST_MORE_VENDORS);
  }
}

void closeLinkEntryPopup() {
  if (scr_link_entry) { lv_obj_del(scr_link_entry); scr_link_entry = nullptr; }
}

void showLinkEntryPopup(bool is_bambu) {
  logSDf("SHOW: LinkEntryPopup bambu=%d", (int)is_bambu);
  link_selected_material[0] = 0;
  link_selected_material_full[0] = 0;
  link_stage3_shown = false;
  closeLinkEntryPopup();
  link_flow_is_bambu = is_bambu;
  link_id_input[0] = '\0';

  scr_link_entry = buildLinkOverlay();

  lv_obj_t* lbl_title = lv_label_create(scr_link_entry);
  lv_label_set_text(lbl_title, is_bambu ? T(STR_LINK_BAMBU_TITLE) : T(STR_LINK_NTAG_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 22);

  lv_obj_t* div = lv_obj_create(scr_link_entry);
  lv_obj_set_size(div, 472, 1);
  lv_obj_set_pos(div, 4, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x1a3060), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t* lbl_ctx = lv_label_create(scr_link_entry);
  char ctx_buf[56];
  if (is_bambu) {
    if (cfg_backend_mode == BACKEND_FILAMAN) {
      snprintf(ctx_buf, sizeof(ctx_buf), g_lang == LANG_DE ? "%s | nicht in FilaMan" : "%s | not in FilaMan", g_tag.material[0] ? g_tag.material : "Bambu Tag");
    } else {
      snprintf(ctx_buf, sizeof(ctx_buf), T(STR_LINK_CTX_NOT_IN_SM), g_tag.material[0] ? g_tag.material : "Bambu Tag");
    }
  } else {
    snprintf(ctx_buf, sizeof(ctx_buf), "UID: %s", link_tag_uid);
  }
  lv_label_set_text(lbl_ctx, ctx_buf);
  lv_obj_set_style_text_color(lbl_ctx, lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_ctx, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_ctx, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_ctx, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_ctx, 450);
  lv_obj_align(lbl_ctx, LV_ALIGN_TOP_MID, 0, 62);

  const int BTN_W = 380;
  const int BTN_H = 60;
  const int BTN_GAP = 10;
  const int Y1 = 100;
  const int Y2 = Y1 + BTN_H + BTN_GAP;
  const int Y3 = Y2 + BTN_H + BTN_GAP;

  lv_obj_t* btn1 = lv_btn_create(scr_link_entry);
  lv_obj_set_size(btn1, BTN_W, BTN_H);
  lv_obj_align(btn1, LV_ALIGN_TOP_MID, 0, Y1);
  lv_obj_set_style_bg_color(btn1, lv_color_hex(0x0a1e30), 0);
  lv_obj_set_style_bg_color(btn1, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn1, 10, 0);
  lv_obj_set_style_shadow_width(btn1, 0, 0);
  lv_obj_set_style_border_width(btn1, 1, 0);
  lv_obj_set_style_border_color(btn1, lv_color_hex(0x1a3060), 0);
  lv_obj_add_event_cb(btn1, [](lv_event_t* e) {
    (void)e;
    link_id_input[0] = '\0';
    showIdInputPopup(link_flow_is_bambu);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l1 = lv_label_create(btn1);
  lv_label_set_text(l1, T(STR_BTN_ENTER_ID));
  lv_obj_set_style_text_color(l1, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(l1);

  lv_obj_t* btn2 = lv_btn_create(scr_link_entry);
  lv_obj_set_size(btn2, BTN_W, BTN_H);
  lv_obj_align(btn2, LV_ALIGN_TOP_MID, 0, Y2);
  lv_obj_set_style_bg_color(btn2, lv_color_hex(0x0a1e30), 0);
  lv_obj_set_style_bg_color(btn2, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn2, 10, 0);
  lv_obj_set_style_shadow_width(btn2, 0, 0);
  lv_obj_set_style_border_width(btn2, 1, 0);
  lv_obj_set_style_border_color(btn2, lv_color_hex(0x1a3060), 0);
  lv_obj_add_event_cb(btn2, [](lv_event_t* e) {
    (void)e;
    fetchAllSpoolsForLink(link_flow_is_bambu, link_flow_is_bambu ? g_tag.material : "");
    if (link_flow_is_bambu) {
      showFilteredSpoolList("", "", "");
    } else {
      showVendorList();
    }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l2 = lv_label_create(btn2);
  lv_label_set_text(l2, T(STR_BTN_FROM_LIST));
  lv_obj_set_style_text_color(l2, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(l2);

  lv_obj_t* btn3 = lv_btn_create(scr_link_entry);
  lv_obj_set_size(btn3, BTN_W, BTN_H - 14);
  lv_obj_align(btn3, LV_ALIGN_TOP_MID, 0, Y3);
  lv_obj_set_style_bg_color(btn3, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn3, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn3, 10, 0);
  lv_obj_set_style_shadow_width(btn3, 0, 0);
  lv_obj_set_style_border_width(btn3, 0, 0);
  lv_obj_add_event_cb(btn3, [](lv_event_t* e) {
    (void)e;
    link_popup_dismissed = true;
    closeLinkEntryPopup();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l3 = lv_label_create(btn3);
  lv_label_set_text(l3, T(STR_CANCEL));
  lv_obj_set_style_text_color(l3, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(l3, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(l3);
}