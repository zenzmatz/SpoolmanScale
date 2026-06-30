#include "settings_screens.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <lvgl.h>
#include <nvs_flash.h>

#include <cstring>

#include "backend_types.h"
#include "filaman_http.h"
#include "filaman_runtime.h"
#include "lang.h"

extern bool sd_available;
extern bool sd_verbose;
extern bool update_available;
extern bool show_bag_pending;
extern bool show_factor_pending;
extern bool show_lastused_pending;
extern bool show_spoolman_pending;
extern bool show_ota_pending;
extern bool show_info_pending;
extern bool show_drying_reminder_pending;
extern bool g_auto_loc_popup;
extern bool g_bag_ui_enabled;
extern bool g_loc_picker_from_popup;
extern bool filaman_config_status_ok;

extern uint8_t g_dry_mode;
extern uint8_t last_used_mode;
extern uint8_t filaman_text_target;
extern uint8_t spoolman_text_target;

extern int bright_normal;
extern int dim_timeout_ms;
extern int sleep_timeout_ms;

extern float cal_factor;
extern float bag_weight_g;

extern BackendMode cfg_backend_mode;
extern char cfg_wifi_ssid[33];
extern char cfg_spoolman_ip[64];
extern char cfg_spoolman_token[160];
extern char cfg_spoolman_code[16];
extern char cfg_filaman_url[96];
extern char cfg_filaman_token[160];
extern char cfg_filaman_code[16];
extern char spoolman_config_status[128];
extern bool spoolman_config_status_ok;
extern char filaman_config_status[128];

extern lv_obj_t* scr_settings;
extern lv_obj_t* scr_connection;
extern lv_obj_t* scr_backend_mode;
extern lv_obj_t* scr_filaman_config;
extern lv_obj_t* scr_spoolman_auth_config;
extern lv_obj_t* scr_filaman_text;
extern lv_obj_t* scr_scale_sub;
extern lv_obj_t* scr_display;
extern lv_obj_t* scr_system;
extern lv_obj_t* lbl_system_badge;
extern lv_obj_t* lbl_fw_badge;
extern lv_obj_t* ta_filaman_text;
extern lv_obj_t* kb_filaman_text;
extern lv_obj_t* lbl_filaman_text_hint;

void logSD(const char* msg);
void buildSubHeader(lv_obj_t* parent, const char* title, lv_event_cb_t back_cb, const char* back_hint = nullptr);
lv_obj_t* buildOverlayScreen();
void addCloseButton(lv_obj_t* parent);
void hideAllOverlays();
void showMainScreen();
void showSettingsScreen();
void showWifiSetupScreen();
void showMoreInfoScreen();
void showExtraFieldsScreen(bool is_setup_flow);
void showLanguageScreen();
void updateBagUiVisibility();
void resetActivityTimer();
void saveBackendMode(BackendMode mode);
const char* backendModeName();
void formatFilamanAuthStatus(char* buf, size_t len);
void formatFilamanTokenStatus(char* buf, size_t len);
void setFilamanConfigStatus(const char* text, bool ok);
void saveFilamanUrl(const char* url);
void saveFilamanToken(const char* token);
void saveFilamanCode(const char* code);
void formatSpoolmanAuthStatus(char* buf, size_t len);
void formatSpoolmanTokenStatus(char* buf, size_t len);
void setSpoolmanConfigStatus(const char* text, bool ok);
void saveSpoolmanToken(const char* token);
void saveSpoolmanCode(const char* code);
bool spoolmanConnectNow(String& status_detail);
void applyDisplayBrightness(uint8_t brightness);

namespace {

void showBackendModeScreen() {
  hideAllOverlays();
  if (scr_backend_mode) { lv_obj_del(scr_backend_mode); scr_backend_mode = nullptr; }
  buildBackendModeScreen();
  lv_obj_clear_flag(scr_backend_mode, LV_OBJ_FLAG_HIDDEN);
}

void showFilamanConfigScreen() {
  hideAllOverlays();
  if (scr_filaman_config) { lv_obj_del(scr_filaman_config); scr_filaman_config = nullptr; }
  buildFilamanConfigScreen();
  lv_obj_clear_flag(scr_filaman_config, LV_OBJ_FLAG_HIDDEN);
}

void showSpoolmanAuthConfigScreen() {
  hideAllOverlays();
  if (scr_spoolman_auth_config) { lv_obj_del(scr_spoolman_auth_config); scr_spoolman_auth_config = nullptr; }
  buildSpoolmanAuthConfigScreen();
  lv_obj_clear_flag(scr_spoolman_auth_config, LV_OBJ_FLAG_HIDDEN);
}

void returnFromFilamanTextScreen() {
  if (filaman_text_target == FILAMAN_FIELD_LOCATION) {
    if (g_loc_picker_from_popup) {
      showMainScreen();
    } else {
      hideAllOverlays();
      showMoreInfoScreen();
    }
    return;
  }

  showFilamanConfigScreen();
}

void returnFromSpoolmanAuthTextScreen() {
  showSpoolmanAuthConfigScreen();
}

lv_obj_t* makeListBtn(lv_obj_t* parent, lv_obj_t* list,
                      const char* ico_sym, const char* title, const char* sub,
                      bool toggle_active = false) {
  (void)parent;
  lv_obj_t* btn = lv_btn_create(list);
  lv_obj_set_size(btn, 456, 64);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1e30), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, toggle_active ? lv_color_hex(0x28d49a) : lv_color_hex(0x1a3050), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_t* ico = lv_label_create(btn);
  lv_label_set_text(ico, ico_sym);
  lv_obj_set_style_text_color(ico, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_20, 0);
  lv_obj_align(ico, LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_width(lbl, 320);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 52, sub && strlen(sub) > 0 ? -10 : 0);
  if (sub && strlen(sub) > 0) {
    lv_obj_t* slbl = lv_label_create(btn);
    lv_label_set_text(slbl, sub);
    lv_obj_set_style_text_color(slbl, toggle_active ? lv_color_hex(0x28d49a) : lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(slbl, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_width(slbl, 320);
    lv_obj_align(slbl, LV_ALIGN_LEFT_MID, 52, 12);
  }
  lv_obj_t* arr = lv_label_create(btn);
  lv_label_set_text(arr, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(arr, lv_color_hex(0x2a4060), 0);
  lv_obj_set_style_text_font(arr, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(arr, LV_ALIGN_RIGHT_MID, -14, 0);
  return btn;
}

}  // namespace

void buildSettingsScreen() {
  logSD("BUILD: SettingsScreen");
  if (sd_verbose) logSD("[verbose] buildSettingsScreen: start");
  scr_settings = buildOverlayScreen();
  char conn_sub[48];
  snprintf(conn_sub, sizeof(conn_sub), "WiFi + %s API", backendModeName());

  lv_obj_t* title = lv_label_create(scr_settings);
  lv_label_set_text(title, T(STR_SETTINGS_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_ext_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t* btn_x = lv_btn_create(scr_settings);
  lv_obj_set_size(btn_x, 44, 44);
  lv_obj_align(btn_x, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_x, 8, 0);
  lv_obj_set_style_shadow_width(btn_x, 0, 0);
  lv_obj_set_style_border_width(btn_x, 0, 0);
  lv_obj_t* lbl_x = lv_label_create(btn_x);
  lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(lbl_x, lv_color_hex(0xff8080), 0);
  lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_x);
  lv_obj_add_event_cb(btn_x, [](lv_event_t* e){ logSD("BTN: Close -> Main"); showMainScreen(); }, LV_EVENT_CLICKED, nullptr);

  struct { const char* icon; const char* label; const char* sub; uint32_t col; } tiles[] = {
    { LV_SYMBOL_WIFI,     T(STR_TILE_CONNECTION), conn_sub,                0x0a1e30 },
    { LV_SYMBOL_DRIVE,    T(STR_TILE_SCALE),      T(STR_TILE_SCALE_SUB),   0x0a1e30 },
    { LV_SYMBOL_IMAGE,    T(STR_TILE_DISPLAY),    T(STR_TILE_DISPLAY_SUB), 0x0a1e30 },
    { LV_SYMBOL_SETTINGS, T(STR_TILE_SYSTEM),     T(STR_TILE_SYSTEM_SUB),  0x0a1e30 },
  };
  int tx[] = { 8, 242, 8, 242 };
  int ty[] = { 60, 60, 186, 186 };

  for (int i = 0; i < 4; i++) {
    lv_obj_t* tile = lv_btn_create(scr_settings);
    lv_obj_set_size(tile, 226, 118);
    lv_obj_set_pos(tile, tx[i], ty[i]);
    lv_obj_set_style_bg_color(tile, lv_color_hex(tiles[i].col), 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(tiles[i].col + 0x101010), LV_STATE_PRESSED);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(tiles[i].col + 0x181818), 0);

    lv_obj_t* ico = lv_label_create(tile);
    lv_label_set_text(ico, tiles[i].icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(0x28d49a), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_24, 0);
    lv_obj_align(ico, LV_ALIGN_TOP_LEFT, 10, 8);

    lv_obj_t* lbl = lv_label_create(tile);
    lv_label_set_text(lbl, tiles[i].label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t* sub = lv_label_create(tile);
    lv_label_set_text(sub, tiles[i].sub);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);

    lv_obj_add_event_cb(tile, [](lv_event_t* e) {
      intptr_t idx = (intptr_t)lv_event_get_user_data(e);
      switch (idx) {
        case 0:
          logSD("UI: Tile -> Connection");
          if (scr_connection) { lv_obj_del(scr_connection); scr_connection = nullptr; }
          buildConnectionScreen();
          if (!scr_connection) buildConnectionScreen(); hideAllOverlays(); lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
          break;
        case 1:
          logSD("UI: Tile -> Scale");
          if (!scr_scale_sub) buildScaleSubScreen();
          if (!scr_scale_sub) buildScaleSubScreen(); hideAllOverlays(); lv_obj_clear_flag(scr_scale_sub, LV_OBJ_FLAG_HIDDEN);
          break;
        case 2:
          logSD("UI: Tile -> Display");
          if (!scr_display) buildDisplayScreen();
          hideAllOverlays(); lv_obj_clear_flag(scr_display, LV_OBJ_FLAG_HIDDEN);
          break;
        case 3:
          logSD("UI: Tile -> System");
          if (!scr_system) buildSystemScreen();
          hideAllOverlays(); lv_obj_clear_flag(scr_system, LV_OBJ_FLAG_HIDDEN);
          break;
      }
      resetActivityTimer();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  lbl_system_badge = lv_obj_create(scr_settings);
  lv_obj_set_size(lbl_system_badge, 14, 14);
  lv_obj_set_pos(lbl_system_badge, 456, 186);
  lv_obj_set_style_radius(lbl_system_badge, 7, 0);
  lv_obj_set_style_bg_color(lbl_system_badge, lv_color_hex(0xe03030), 0);
  lv_obj_set_style_border_color(lbl_system_badge, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(lbl_system_badge, 2, 0);
  lv_obj_set_style_pad_all(lbl_system_badge, 0, 0);
  lv_obj_clear_flag(lbl_system_badge, LV_OBJ_FLAG_SCROLLABLE);
  if (!update_available) lv_obj_add_flag(lbl_system_badge, LV_OBJ_FLAG_HIDDEN);
  if (sd_verbose) logSD("[verbose] buildSettingsScreen: done");
}

void buildConnectionScreen() {
  logSD("BUILD: ConnectionScreen");
  if (sd_verbose) logSD("[verbose] buildConnectionScreen: start");
  scr_connection = buildOverlayScreen();
  buildSubHeader(scr_connection, T(STR_TILE_CONNECTION),
    [](lv_event_t* e){ logSD("BTN: Back -> Settings"); showSettingsScreen(); });

  const int BTN_W = 456, BTN_H = 58, BTN_X = 12;
  const int BTN_Y[] = { 54, 120, 186, 252 };

  char backend_sub[24];
  char service_label[24];
  char service_sub[96];
  char aux_label[24];
  char aux_sub[64];
  snprintf(backend_sub, sizeof(backend_sub), "%s", backendModeName());
  if (cfg_backend_mode == BACKEND_FILAMAN) {
    snprintf(service_label, sizeof(service_label), "FilaMan Config");
    snprintf(service_sub, sizeof(service_sub), "%s", cfg_filaman_url[0] ? cfg_filaman_url : "Not set");
    snprintf(aux_label, sizeof(aux_label), "Device Auth");
    formatFilamanAuthStatus(aux_sub, sizeof(aux_sub));
  } else {
    snprintf(service_label, sizeof(service_label), "Spoolman Server");
    snprintf(service_sub, sizeof(service_sub), "%s", cfg_spoolman_ip[0] ? cfg_spoolman_ip : T(STR_BTN_WIFI_NONE));
    snprintf(aux_label, sizeof(aux_label), "Hardware Auth");
    formatSpoolmanAuthStatus(aux_sub, sizeof(aux_sub));
  }

  auto addConnButton = [&](int y, const char* icon, const char* label, const char* sub, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(scr_connection);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_pos(btn, BTN_X, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1e30), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x1a3050), 0);

    lv_obj_t* ico = lv_label_create(btn);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(0x28d49a), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_22, 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 18, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 56, 8);

    lv_obj_t* sub_lbl = lv_label_create(btn);
    lv_label_set_text(sub_lbl, sub);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(sub_lbl, LV_ALIGN_TOP_LEFT, 56, 31);
    lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sub_lbl, 380);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  };

  addConnButton(BTN_Y[0], LV_SYMBOL_WIFI, T(STR_BTN_WIFI_SETTINGS),
    cfg_wifi_ssid[0] ? cfg_wifi_ssid : T(STR_BTN_WIFI_NONE),
    [](lv_event_t* e){ logSD("BTN: Conn -> WifiSetup"); showWifiSetupScreen(); });

  addConnButton(BTN_Y[1], LV_SYMBOL_REFRESH, "Backend Mode", backend_sub,
    [](lv_event_t* e){ logSD("BTN: Conn -> BackendMode"); showBackendModeScreen(); });

  if (cfg_backend_mode == BACKEND_FILAMAN) {
    addConnButton(BTN_Y[2], LV_SYMBOL_SETTINGS, service_label, service_sub,
      [](lv_event_t* e){ logSD("BTN: Conn -> FilaMan Config"); showFilamanConfigScreen(); });
    addConnButton(BTN_Y[3], LV_SYMBOL_KEYBOARD, aux_label, aux_sub,
      [](lv_event_t* e){ logSD("BTN: Conn -> FilaMan Auth"); showFilamanConfigScreen(); });
  } else {
    addConnButton(BTN_Y[2], LV_SYMBOL_SETTINGS, service_label, service_sub,
      [](lv_event_t* e){ logSD("BTN: Conn -> Spoolman IP"); show_spoolman_pending = true; });
    addConnButton(BTN_Y[3], LV_SYMBOL_KEYBOARD, aux_label, aux_sub,
      [](lv_event_t* e){ logSD("BTN: Conn -> Spoolman Auth"); showSpoolmanAuthConfigScreen(); });
  }
  if (sd_verbose) logSD("[verbose] buildConnectionScreen: done");
}

void buildBackendModeScreen() {
  logSD("BUILD: BackendModeScreen");
  scr_backend_mode = buildOverlayScreen();
  buildSubHeader(scr_backend_mode, "Backend Mode",
    [](lv_event_t* e){
      if (!scr_connection) buildConnectionScreen();
      hideAllOverlays();
      lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
    });
  addCloseButton(scr_backend_mode);

  lv_obj_t* btn_spoolman = lv_btn_create(scr_backend_mode);
  lv_obj_set_size(btn_spoolman, 210, 58);
  lv_obj_set_pos(btn_spoolman, 12, 70);
  bool spoolman_active = (cfg_backend_mode == BACKEND_SPOOLMAN);
  lv_obj_set_style_bg_color(btn_spoolman, lv_color_hex(spoolman_active ? 0x1a3020 : 0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_spoolman, lv_color_hex(0x2a5030), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_spoolman, 1, 0);
  lv_obj_set_style_border_color(btn_spoolman, lv_color_hex(spoolman_active ? 0x28d49a : 0x1a3060), 0);
  lv_obj_set_style_radius(btn_spoolman, 8, 0);
  lv_obj_set_style_shadow_width(btn_spoolman, 0, 0);
  lv_obj_add_event_cb(btn_spoolman, [](lv_event_t* e) {
    saveBackendMode(BACKEND_SPOOLMAN);
    if (scr_connection) { lv_obj_del(scr_connection); scr_connection = nullptr; }
    buildConnectionScreen();
    hideAllOverlays();
    lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_spoolman = lv_label_create(btn_spoolman);
  lv_label_set_text(lbl_spoolman, "Spoolman");
  lv_obj_set_style_text_color(lbl_spoolman, lv_color_hex(spoolman_active ? 0x40c080 : 0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_spoolman, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_spoolman);

  lv_obj_t* btn_filaman = lv_btn_create(scr_backend_mode);
  lv_obj_set_size(btn_filaman, 210, 58);
  lv_obj_set_pos(btn_filaman, 238, 70);
  bool filaman_active = (cfg_backend_mode == BACKEND_FILAMAN);
  lv_obj_set_style_bg_color(btn_filaman, lv_color_hex(filaman_active ? 0x1a3020 : 0x0a1828), 0);
  lv_obj_set_style_bg_color(btn_filaman, lv_color_hex(0x2a5030), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_filaman, 1, 0);
  lv_obj_set_style_border_color(btn_filaman, lv_color_hex(filaman_active ? 0x28d49a : 0x1a3060), 0);
  lv_obj_set_style_radius(btn_filaman, 8, 0);
  lv_obj_set_style_shadow_width(btn_filaman, 0, 0);
  lv_obj_add_event_cb(btn_filaman, [](lv_event_t* e) {
    saveBackendMode(BACKEND_FILAMAN);
    if (scr_connection) { lv_obj_del(scr_connection); scr_connection = nullptr; }
    buildConnectionScreen();
    hideAllOverlays();
    lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_filaman = lv_label_create(btn_filaman);
  lv_label_set_text(lbl_filaman, "FilaMan");
  lv_obj_set_style_text_color(lbl_filaman, lv_color_hex(filaman_active ? 0x40c080 : 0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_filaman, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_filaman);

  lv_obj_t* lbl_desc = lv_label_create(scr_backend_mode);
  lv_label_set_text(lbl_desc,
    "Spoolman mode keeps the current REST integration.\n"
    "FilaMan mode stores device settings now; API runtime\n"
    "integration comes in the next phase.");
  lv_obj_set_style_text_color(lbl_desc, lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_desc, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_desc, 420);
  lv_obj_align(lbl_desc, LV_ALIGN_TOP_MID, 0, 150);
}

void buildFilamanConfigScreen() {
  logSD("BUILD: FilamanConfigScreen");
  scr_filaman_config = buildOverlayScreen();
  buildSubHeader(scr_filaman_config, "FilaMan Config",
    [](lv_event_t* e){
      if (!scr_connection) buildConnectionScreen();
      hideAllOverlays();
      lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
    });
  addCloseButton(scr_filaman_config);

  auto addCfgButton = [&](int y, const char* label, const char* sub, uint8_t target) {
    lv_obj_t* btn = lv_btn_create(scr_filaman_config);
    lv_obj_set_size(btn, 456, 58);
    lv_obj_set_pos(btn, 12, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1e30), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x1a3050), 0);
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      uint8_t target = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      showFilamanTextScreen(target);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)target);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 16, 8);

    lv_obj_t* sub_lbl = lv_label_create(btn);
    lv_label_set_text(sub_lbl, sub);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(sub_lbl, LV_ALIGN_TOP_LEFT, 16, 31);
    lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sub_lbl, 420);
  };

  char token_status[48];
  const char* connect_sub = filaman_config_status[0]
    ? filaman_config_status
    : (cfg_filaman_token[0]
      ? "Send a heartbeat and verify the stored device token."
      : "Use the saved code to fetch a device token immediately.");
  formatFilamanTokenStatus(token_status, sizeof(token_status));
  addCfgButton(58, "Server URL", cfg_filaman_url[0] ? cfg_filaman_url : "Not set", FILAMAN_FIELD_URL);
  addCfgButton(124, "Device Token", token_status, FILAMAN_FIELD_TOKEN);
  addCfgButton(190, "Device Code", cfg_filaman_code[0] ? cfg_filaman_code : "Not set", FILAMAN_FIELD_CODE);

  lv_obj_t* btn_connect = lv_btn_create(scr_filaman_config);
  lv_obj_set_size(btn_connect, 456, 52);
  lv_obj_set_pos(btn_connect, 12, 256);
  lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x1a3020), 0);
  lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x2a5030), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_connect, 10, 0);
  lv_obj_set_style_shadow_width(btn_connect, 0, 0);
  lv_obj_set_style_border_width(btn_connect, 1, 0);
  lv_obj_set_style_border_color(btn_connect, lv_color_hex(0x28d49a), 0);
  lv_obj_add_event_cb(btn_connect, [](lv_event_t* e){
    String status_detail;
    bool ok = filamanConnectNow(status_detail);
    setFilamanConfigStatus(status_detail.c_str(), ok);
    showFilamanConfigScreen();
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* lbl_connect = lv_label_create(btn_connect);
  lv_label_set_text(lbl_connect, cfg_filaman_token[0] ? "Connect now" : "Register + connect");
  lv_obj_set_style_text_color(lbl_connect, lv_color_hex(0x80ffb0), 0);
  lv_obj_set_style_text_font(lbl_connect, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_connect, LV_ALIGN_TOP_LEFT, 16, 8);

  lv_obj_t* lbl_connect_sub = lv_label_create(btn_connect);
  lv_label_set_text(lbl_connect_sub, connect_sub);
  lv_obj_set_style_text_color(lbl_connect_sub,
    filaman_config_status[0]
      ? (filaman_config_status_ok ? lv_color_hex(0x80ffb0) : lv_color_hex(0xff8080))
      : lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_connect_sub, &lv_font_montserrat_ext_12, 0);
  lv_obj_align(lbl_connect_sub, LV_ALIGN_TOP_LEFT, 16, 30);
  lv_obj_set_width(lbl_connect_sub, 420);
  lv_label_set_long_mode(lbl_connect_sub, LV_LABEL_LONG_DOT);
}

void showFilamanTextScreen(uint8_t field_target) {
  filaman_text_target = field_target;
  hideAllOverlays();
  if (scr_filaman_text) { lv_obj_del(scr_filaman_text); scr_filaman_text = nullptr; }
  ta_filaman_text = nullptr;
  kb_filaman_text = nullptr;
  lbl_filaman_text_hint = nullptr;
  buildFilamanTextScreen();
  lv_obj_clear_flag(scr_filaman_text, LV_OBJ_FLAG_HIDDEN);
}

void buildFilamanTextScreen() {
  logSD("BUILD: FilamanTextScreen");
  const char* title = "FilaMan";
  const char* initial = "";
  const char* placeholder = "Value";
  const char* hint_text = "Press OK on the keyboard to save.";

  if (filaman_text_target == FILAMAN_FIELD_URL) {
    title = "FilaMan URL";
    initial = cfg_filaman_url;
    placeholder = "http://host:port";
  } else if (filaman_text_target == FILAMAN_FIELD_TOKEN) {
    title = "Device Token";
    initial = cfg_filaman_token;
    placeholder = "dev.1...";
  } else if (filaman_text_target == FILAMAN_FIELD_CODE) {
    title = "Device Code";
    initial = cfg_filaman_code;
    placeholder = "6-digit code";
  } else {
    title = "Locate Spool";
    initial = "";
    placeholder = "Location tag or ID";
    hint_text = "Press OK to send a FilaMan locate request.";
  }

  scr_filaman_text = buildOverlayScreen();
  buildSubHeader(scr_filaman_text, title,
    [](lv_event_t* e){ returnFromFilamanTextScreen(); });

  lv_obj_t* btn_close = lv_btn_create(scr_filaman_text);
  lv_obj_set_size(btn_close, 44, 44);
  lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_close, 8, 0);
  lv_obj_set_style_shadow_width(btn_close, 0, 0);
  lv_obj_set_style_border_width(btn_close, 0, 0);
  lv_obj_add_event_cb(btn_close, [](lv_event_t* e){
    if (filaman_text_target == FILAMAN_FIELD_LOCATION) returnFromFilamanTextScreen();
    else showMainScreen();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_close = lv_label_create(btn_close);
  lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_color(lbl_close, lv_color_hex(0xff8080), 0);
  lv_obj_center(lbl_close);

  lbl_filaman_text_hint = lv_label_create(scr_filaman_text);
  lv_label_set_text(lbl_filaman_text_hint, hint_text);
  lv_obj_set_style_text_color(lbl_filaman_text_hint, lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_filaman_text_hint, &lv_font_montserrat_ext_14, 0);
  lv_obj_align(lbl_filaman_text_hint, LV_ALIGN_TOP_MID, 0, 52);

  ta_filaman_text = lv_textarea_create(scr_filaman_text);
  lv_textarea_set_one_line(ta_filaman_text, true);
  lv_textarea_set_password_mode(ta_filaman_text, false);
  lv_textarea_set_placeholder_text(ta_filaman_text, placeholder);
  lv_textarea_set_text(ta_filaman_text, initial);
  lv_obj_set_size(ta_filaman_text, 420, 44);
  lv_obj_align(ta_filaman_text, LV_ALIGN_TOP_MID, 0, 74);
  lv_obj_set_style_text_font(ta_filaman_text, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_color(ta_filaman_text, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_color(ta_filaman_text, lv_color_hex(0x1e2e4a), 0);
  lv_obj_set_style_border_color(ta_filaman_text, lv_color_hex(0x2a4080), 0);

  kb_filaman_text = lv_keyboard_create(scr_filaman_text);
  lv_keyboard_set_textarea(kb_filaman_text, ta_filaman_text);
  lv_obj_set_size(kb_filaman_text, 480, 160);
  lv_obj_align(kb_filaman_text, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb_filaman_text, lv_color_hex(0x182238), 0);
  lv_obj_set_style_border_width(kb_filaman_text, 0, 0);
  lv_obj_add_event_cb(kb_filaman_text, [](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_READY) {
      const char* value = lv_textarea_get_text(ta_filaman_text);
      if (filaman_text_target == FILAMAN_FIELD_URL) {
        saveFilamanUrl(value);
        setFilamanConfigStatus("URL saved. Tap Connect now.", false);
        showFilamanConfigScreen();
      } else if (filaman_text_target == FILAMAN_FIELD_TOKEN) {
        saveFilamanToken(value);
        setFilamanConfigStatus("Token saved. Tap Connect now.", false);
        showFilamanConfigScreen();
      } else if (filaman_text_target == FILAMAN_FIELD_CODE) {
        saveFilamanCode(value);
        setFilamanConfigStatus("Code saved. Tap Connect now.", false);
        showFilamanConfigScreen();
      } else {
        String status_detail;
        if (filamanLocateCurrentSpool(value, status_detail)) {
          returnFromFilamanTextScreen();
        } else if (lbl_filaman_text_hint) {
          lv_label_set_text(lbl_filaman_text_hint, status_detail.c_str());
          lv_obj_set_style_text_color(lbl_filaman_text_hint, lv_color_hex(0xff8080), 0);
        }
      }
    }
  }, LV_EVENT_ALL, nullptr);
}

void buildSpoolmanAuthConfigScreen() {
  logSD("BUILD: SpoolmanAuthConfigScreen");
  scr_spoolman_auth_config = buildOverlayScreen();
  buildSubHeader(scr_spoolman_auth_config, "Spoolman Auth",
    [](lv_event_t* e){
      if (!scr_connection) buildConnectionScreen();
      hideAllOverlays();
      lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
    });
  addCloseButton(scr_spoolman_auth_config);

  auto addCfgButton = [&](int y, const char* label, const char* sub, uint8_t target) {
    lv_obj_t* btn = lv_btn_create(scr_spoolman_auth_config);
    lv_obj_set_size(btn, 456, 58);
    lv_obj_set_pos(btn, 12, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1e30), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x1a3050), 0);
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      uint8_t target = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      showSpoolmanAuthTextScreen(target);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)target);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 16, 8);

    lv_obj_t* sub_lbl = lv_label_create(btn);
    lv_label_set_text(sub_lbl, sub);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(sub_lbl, LV_ALIGN_TOP_LEFT, 16, 31);
    lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sub_lbl, 420);
  };

  char token_status[48];
  const char* connect_sub = spoolman_config_status[0]
    ? spoolman_config_status
    : (cfg_spoolman_token[0]
      ? "Verify the stored Spoolman API token."
      : "Use the saved hardware code to fetch a token.");
  formatSpoolmanTokenStatus(token_status, sizeof(token_status));
  addCfgButton(76, "Device Token", token_status, SPOOLMAN_FIELD_TOKEN);
  addCfgButton(142, "Hardware Code", cfg_spoolman_code[0] ? cfg_spoolman_code : "Not set", SPOOLMAN_FIELD_CODE);

  lv_obj_t* btn_connect = lv_btn_create(scr_spoolman_auth_config);
  lv_obj_set_size(btn_connect, 456, 58);
  lv_obj_set_pos(btn_connect, 12, 222);
  lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x1a3020), 0);
  lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x2a5030), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_connect, 10, 0);
  lv_obj_set_style_shadow_width(btn_connect, 0, 0);
  lv_obj_set_style_border_width(btn_connect, 1, 0);
  lv_obj_set_style_border_color(btn_connect, lv_color_hex(0x28d49a), 0);
  lv_obj_add_event_cb(btn_connect, [](lv_event_t* e){
    String status_detail;
    bool ok = spoolmanConnectNow(status_detail);
    setSpoolmanConfigStatus(status_detail.c_str(), ok);
    showSpoolmanAuthConfigScreen();
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* lbl_connect = lv_label_create(btn_connect);
  lv_label_set_text(lbl_connect, cfg_spoolman_token[0] ? "Connect now" : "Register + connect");
  lv_obj_set_style_text_color(lbl_connect, lv_color_hex(0x80ffb0), 0);
  lv_obj_set_style_text_font(lbl_connect, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_connect, LV_ALIGN_TOP_LEFT, 16, 8);

  lv_obj_t* lbl_connect_sub = lv_label_create(btn_connect);
  lv_label_set_text(lbl_connect_sub, connect_sub);
  lv_obj_set_style_text_color(lbl_connect_sub,
    spoolman_config_status[0]
      ? (spoolman_config_status_ok ? lv_color_hex(0x80ffb0) : lv_color_hex(0xff8080))
      : lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_connect_sub, &lv_font_montserrat_ext_12, 0);
  lv_obj_align(lbl_connect_sub, LV_ALIGN_TOP_LEFT, 16, 33);
  lv_obj_set_width(lbl_connect_sub, 420);
  lv_label_set_long_mode(lbl_connect_sub, LV_LABEL_LONG_DOT);
}

void showSpoolmanAuthTextScreen(uint8_t field_target) {
  spoolman_text_target = field_target;
  hideAllOverlays();
  if (scr_filaman_text) { lv_obj_del(scr_filaman_text); scr_filaman_text = nullptr; }
  ta_filaman_text = nullptr;
  kb_filaman_text = nullptr;
  lbl_filaman_text_hint = nullptr;
  buildSpoolmanAuthTextScreen();
  lv_obj_clear_flag(scr_filaman_text, LV_OBJ_FLAG_HIDDEN);
}

void buildSpoolmanAuthTextScreen() {
  logSD("BUILD: SpoolmanAuthTextScreen");
  const char* title = spoolman_text_target == SPOOLMAN_FIELD_TOKEN ? "Device Token" : "Hardware Code";
  const char* initial = spoolman_text_target == SPOOLMAN_FIELD_TOKEN ? cfg_spoolman_token : cfg_spoolman_code;
  const char* placeholder = spoolman_text_target == SPOOLMAN_FIELD_TOKEN ? "spat_..." : "6-digit code";

  scr_filaman_text = buildOverlayScreen();
  buildSubHeader(scr_filaman_text, title,
    [](lv_event_t* e){ returnFromSpoolmanAuthTextScreen(); });

  lv_obj_t* btn_close = lv_btn_create(scr_filaman_text);
  lv_obj_set_size(btn_close, 44, 44);
  lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x3a1010), 0);
  lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x602020), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_close, 8, 0);
  lv_obj_set_style_shadow_width(btn_close, 0, 0);
  lv_obj_set_style_border_width(btn_close, 0, 0);
  lv_obj_add_event_cb(btn_close, [](lv_event_t* e){ showMainScreen(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_close = lv_label_create(btn_close);
  lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_color(lbl_close, lv_color_hex(0xff8080), 0);
  lv_obj_center(lbl_close);

  lbl_filaman_text_hint = lv_label_create(scr_filaman_text);
  lv_label_set_text(lbl_filaman_text_hint, "Press OK on the keyboard to save.");
  lv_obj_set_style_text_color(lbl_filaman_text_hint, lv_color_hex(0x4a6fa0), 0);
  lv_obj_set_style_text_font(lbl_filaman_text_hint, &lv_font_montserrat_ext_14, 0);
  lv_obj_align(lbl_filaman_text_hint, LV_ALIGN_TOP_MID, 0, 52);

  ta_filaman_text = lv_textarea_create(scr_filaman_text);
  lv_textarea_set_one_line(ta_filaman_text, true);
  lv_textarea_set_password_mode(ta_filaman_text, false);
  lv_textarea_set_placeholder_text(ta_filaman_text, placeholder);
  lv_textarea_set_text(ta_filaman_text, initial);
  lv_obj_set_size(ta_filaman_text, 420, 44);
  lv_obj_align(ta_filaman_text, LV_ALIGN_TOP_MID, 0, 74);
  lv_obj_set_style_text_font(ta_filaman_text, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_color(ta_filaman_text, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_color(ta_filaman_text, lv_color_hex(0x1e2e4a), 0);
  lv_obj_set_style_border_color(ta_filaman_text, lv_color_hex(0x2a4080), 0);

  kb_filaman_text = lv_keyboard_create(scr_filaman_text);
  lv_keyboard_set_textarea(kb_filaman_text, ta_filaman_text);
  lv_obj_set_size(kb_filaman_text, 480, 160);
  lv_obj_align(kb_filaman_text, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb_filaman_text, lv_color_hex(0x182238), 0);
  lv_obj_set_style_border_width(kb_filaman_text, 0, 0);
  lv_obj_add_event_cb(kb_filaman_text, [](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_READY) {
      const char* value = lv_textarea_get_text(ta_filaman_text);
      if (spoolman_text_target == SPOOLMAN_FIELD_TOKEN) {
        saveSpoolmanToken(value);
        setSpoolmanConfigStatus("Token saved. Tap Connect now.", false);
      } else {
        saveSpoolmanCode(value);
        setSpoolmanConfigStatus("Code saved. Tap Connect now.", false);
      }
      showSpoolmanAuthConfigScreen();
    }
  }, LV_EVENT_ALL, nullptr);
}

void buildScaleSubScreen() {
  logSD("BUILD: ScaleSubScreen");
  if (sd_verbose) logSD("[verbose] buildScaleSubScreen: start");
  scr_scale_sub = buildOverlayScreen();
  buildSubHeader(scr_scale_sub, T(STR_SCALE_TITLE),
    [](lv_event_t* e){ logSD("BTN: Back -> Settings"); showSettingsScreen(); });

  lv_obj_t* list = lv_obj_create(scr_scale_sub);
  lv_obj_set_size(list, 480, 263);
  lv_obj_set_pos(list, 0, 57);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_left(list, 12, 0);
  lv_obj_set_style_pad_right(list, 12, 0);
  lv_obj_set_style_pad_top(list, 6, 0);
  lv_obj_set_style_pad_bottom(list, 6, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);

  { char buf_s[8]; strncpy(buf_s, g_bag_ui_enabled ? "ON" : "OFF", sizeof(buf_s)-1);
    buf_s[sizeof(buf_s)-1] = 0;
    lv_obj_t* btn = makeListBtn(scr_scale_sub, list, LV_SYMBOL_EYE_OPEN, T(STR_BTN_BAG_UI), buf_s, g_bag_ui_enabled);
    lv_obj_t* arr_lbl = lv_obj_get_child(btn, -1);
    if (arr_lbl) {
      lv_label_set_text(arr_lbl, g_bag_ui_enabled ? "ON" : "OFF");
      lv_obj_set_style_text_color(arr_lbl, g_bag_ui_enabled ? lv_color_hex(0x28d49a) : lv_color_hex(0x4a6fa0), 0);
      lv_obj_set_style_text_font(arr_lbl, &lv_font_montserrat_ext_14, 0);
    }
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      logSD("BTN: Scale-Sub -> Bag UI Toggle");
      g_bag_ui_enabled = !g_bag_ui_enabled;
      Preferences prefs; prefs.begin("spoolscale", false);
      prefs.putBool("bag_ui", g_bag_ui_enabled);
      prefs.end();
      updateBagUiVisibility();
      if (scr_scale_sub) { lv_obj_del(scr_scale_sub); scr_scale_sub = nullptr; }
      buildScaleSubScreen();
      lv_obj_clear_flag(scr_scale_sub, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr); }

  if (g_bag_ui_enabled) { char bag_sub[32]; snprintf(bag_sub, sizeof(bag_sub), T(STR_BAG_CURRENT), bag_weight_g);
    lv_obj_t* btn = makeListBtn(scr_scale_sub, list, LV_SYMBOL_DRIVE, T(STR_BTN_BAGWEIGHT), bag_sub);
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      logSD("BTN: Scale-Sub -> Bag Weight");
      show_bag_pending = true;
    }, LV_EVENT_CLICKED, nullptr); }

  { char buf_t[40]; strncpy(buf_t, T(STR_BTN_DRYING_REMINDER), sizeof(buf_t)-1);
    char buf_s[24];
    const char* mode_lbl[] = { T(STR_DRY_MODE_OFF), T(STR_DRY_MODE_MATERIAL), T(STR_DRY_MODE_MANUAL) };
    strncpy(buf_s, mode_lbl[g_dry_mode < 3 ? g_dry_mode : 0], sizeof(buf_s)-1);
    lv_obj_t* btn = makeListBtn(scr_scale_sub, list, LV_SYMBOL_WARNING, buf_t, buf_s);
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      logSD("BTN: Scale-Sub -> Drying Reminder");
      show_drying_reminder_pending = true;
    }, LV_EVENT_CLICKED, nullptr); }

  { char buf_t[40]; strncpy(buf_t, T(STR_BTN_AUTO_LOC_POPUP), sizeof(buf_t)-1);
    char buf_s[8]; strncpy(buf_s, g_auto_loc_popup ? "ON" : "OFF", sizeof(buf_s)-1);
    lv_obj_t* btn = makeListBtn(scr_scale_sub, list, LV_SYMBOL_GPS, buf_t, buf_s, g_auto_loc_popup);
    lv_obj_t* arr_lbl = lv_obj_get_child(btn, -1);
    if (arr_lbl) {
      lv_label_set_text(arr_lbl, g_auto_loc_popup ? "ON" : "OFF");
      lv_obj_set_style_text_color(arr_lbl, g_auto_loc_popup ? lv_color_hex(0x28d49a) : lv_color_hex(0x4a6fa0), 0);
      lv_obj_set_style_text_font(arr_lbl, &lv_font_montserrat_ext_14, 0);
    }
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      logSD("BTN: Scale-Sub -> Auto Location Popup Toggle");
      g_auto_loc_popup = !g_auto_loc_popup;
      Preferences prefs; prefs.begin("spoolscale", false);
      prefs.putBool("auto_loc_popup", g_auto_loc_popup);
      prefs.end();
      if (scr_scale_sub) { lv_obj_del(scr_scale_sub); scr_scale_sub = nullptr; }
      buildScaleSubScreen();
      lv_obj_clear_flag(scr_scale_sub, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr); }

  { char buf_t[32]; strncpy(buf_t, T(STR_BTN_LASTUSED_MODE), sizeof(buf_t)-1);
    char buf_s[48]; strncpy(buf_s, T(STR_BTN_LASTUSED_MODE_SUB), sizeof(buf_s)-1);
    lv_obj_t* btn = makeListBtn(scr_scale_sub, list, LV_SYMBOL_SAVE, buf_t, buf_s);
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      logSD("BTN: Scale-Sub -> Last Used Mode");
      show_lastused_pending = true;
    }, LV_EVENT_CLICKED, nullptr); }

  { char cal_sub[32]; snprintf(cal_sub, sizeof(cal_sub), T(STR_CAL_FACTOR_SHORT), cal_factor);
    lv_obj_t* btn = makeListBtn(scr_scale_sub, list, LV_SYMBOL_EDIT, T(STR_BTN_CALIBRATE), cal_sub);
    lv_obj_add_event_cb(btn, [](lv_event_t* e){
      logSD("BTN: Scale-Sub -> Calibration");
      show_factor_pending = true;
    }, LV_EVENT_CLICKED, nullptr); }

  if (sd_verbose) logSD("[verbose] buildScaleSubScreen: done");
}

void buildDisplayScreen() {
  logSD("BUILD: DisplayScreen");
  if (sd_verbose) logSD("[verbose] buildDisplayScreen: start");
  scr_display = buildOverlayScreen();
  buildSubHeader(scr_display, T(STR_DISPLAY_TITLE),
    [](lv_event_t* e){ logSD("BTN: Back -> Settings"); showSettingsScreen(); });

  lv_obj_t* lbl_bright = lv_label_create(scr_display);
  lv_label_set_text(lbl_bright, T(STR_BRIGHT_LABEL));
  lv_obj_set_style_text_color(lbl_bright, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_bright, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_bright, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_bright, LV_ALIGN_TOP_MID, 0, 54);

  lv_obj_t* slider = lv_slider_create(scr_display);
  lv_obj_set_size(slider, 456, 20);
  lv_obj_set_pos(slider, 12, 76);
  lv_slider_set_range(slider, 10, 255);
  lv_slider_set_value(slider, bright_normal, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x1a3060), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x28d49a), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x28d49a), LV_PART_KNOB);

  lv_obj_add_event_cb(slider, [](lv_event_t* e) {
    lv_obj_t* s = lv_event_get_target(e);
    int val = lv_slider_get_value(s);
    bright_normal = val;
    applyDisplayBrightness((uint8_t)val);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(slider, [](lv_event_t* e) {
    int val = lv_slider_get_value(lv_event_get_target(e));
    Preferences p; p.begin("spoolscale", false);
    p.putUChar("bright", (uint8_t)val);
    p.end();
    Serial.printf("Brightness saved: %d\n", val);
  }, LV_EVENT_RELEASED, nullptr);

  lv_obj_t* lbl_dim = lv_label_create(scr_display);
  lv_label_set_text(lbl_dim, T(STR_DIM_LABEL));
  lv_obj_set_style_text_color(lbl_dim, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_dim, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_dim, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_dim, LV_ALIGN_TOP_MID, 0, 108);

  int dim_vals[] = {1, 2, 5, 10};
  int cur_dim = dim_timeout_ms / 60000;
  const int BTN_W = 88, BTN_H = 36, BTN_GAP = 8;
  const int BTN_START_X = (480 - 4*BTN_W - 3*BTN_GAP) / 2;
  for (int i = 0; i < 4; i++) {
    lv_obj_t* b = lv_btn_create(scr_display);
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_set_pos(b, BTN_START_X + i * (BTN_W + BTN_GAP), 130);
    bool active = (cur_dim == dim_vals[i]);
    lv_obj_set_style_bg_color(b, active ? lv_color_hex(0x28d49a) : lv_color_hex(0x1a3060), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    char buf[8]; snprintf(buf, sizeof(buf), "%d Min", dim_vals[i]);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, active ? lv_color_hex(0x0a1020) : lv_color_hex(0xc8d8f0), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      int val = (intptr_t)lv_event_get_user_data(e);
      dim_timeout_ms = val * 60000;
      Preferences prefs; prefs.begin("spoolscale", false);
      prefs.putUInt("dim_min", val);
      prefs.end();
      if (scr_display) { lv_obj_del(scr_display); scr_display = nullptr; }
      buildDisplayScreen();
      lv_obj_clear_flag(scr_display, LV_OBJ_FLAG_HIDDEN);
      Serial.printf("Dim timeout: %d min\n", val);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)dim_vals[i]);
  }

  lv_obj_t* lbl_sleep = lv_label_create(scr_display);
  lv_label_set_text(lbl_sleep, T(STR_SLEEP_LABEL));
  lv_obj_set_style_text_color(lbl_sleep, lv_color_hex(0xc8d8f0), 0);
  lv_obj_set_style_text_font(lbl_sleep, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_sleep, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_sleep, LV_ALIGN_TOP_MID, 0, 178);

  int sleep_vals[] = {10, 20, 30, 60};
  int cur_sleep = sleep_timeout_ms / 60000;
  for (int i = 0; i < 4; i++) {
    lv_obj_t* b = lv_btn_create(scr_display);
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_set_pos(b, BTN_START_X + i * (BTN_W + BTN_GAP), 200);
    bool active = (cur_sleep == sleep_vals[i]);
    lv_obj_set_style_bg_color(b, active ? lv_color_hex(0x28d49a) : lv_color_hex(0x1a3060), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    char buf[8]; snprintf(buf, sizeof(buf), "%d Min", sleep_vals[i]);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, active ? lv_color_hex(0x0a1020) : lv_color_hex(0xc8d8f0), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      int val = (intptr_t)lv_event_get_user_data(e);
      sleep_timeout_ms = val * 60000;
      Preferences prefs; prefs.begin("spoolscale", false);
      prefs.putUInt("sleep_min", val);
      prefs.end();
      if (scr_display) { lv_obj_del(scr_display); scr_display = nullptr; }
      buildDisplayScreen();
      lv_obj_clear_flag(scr_display, LV_OBJ_FLAG_HIDDEN);
      Serial.printf("Sleep timeout: %d min\n", val);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)sleep_vals[i]);
  }

  lv_obj_t* hint = lv_label_create(scr_display);
  lv_label_set_text(hint, T(STR_DISPLAY_HINT));
  lv_obj_set_style_text_color(hint, lv_color_hex(0x2a4060), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, 440);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
  if (sd_verbose) logSD("[verbose] buildDisplayScreen: done");
}

void buildSystemScreen() {
  logSD("BUILD: SystemScreen");
  if (sd_verbose) logSD("[verbose] buildSystemScreen: start");
  scr_system = buildOverlayScreen();
  buildSubHeader(scr_system, T(STR_SYSTEM_TITLE),
    [](lv_event_t* e){ logSD("BTN: Back -> Settings"); showSettingsScreen(); });

  const int BTN_W = 456, BTN_H = 62, RESET_H = 44, BTN_GAP = 5, BTN_X = 12, BTN_Y0 = 52;

  lv_obj_t* btn_lang = lv_btn_create(scr_system);
  lv_obj_set_size(btn_lang, BTN_W, BTN_H);
  lv_obj_set_pos(btn_lang, BTN_X, BTN_Y0);
  lv_obj_set_style_bg_color(btn_lang, lv_color_hex(0x0a1a2a), 0);
  lv_obj_set_style_bg_color(btn_lang, lv_color_hex(0x1a2a40), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_lang, 10, 0);
  lv_obj_set_style_shadow_width(btn_lang, 0, 0);
  lv_obj_set_style_border_width(btn_lang, 1, 0);
  lv_obj_set_style_border_color(btn_lang, lv_color_hex(0x1a2a40), 0);
  { lv_obj_t* ico = lv_label_create(btn_lang);
    lv_label_set_text(ico, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(ico, lv_color_hex(0x28d49a), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_24, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t* lbl = lv_label_create(btn_lang);
    lv_label_set_text(lbl, T(STR_LANG_TITLE));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t* sub = lv_label_create(btn_lang);
    lv_label_set_text(sub, T(STR_BTN_LANG_SUB));
    lv_obj_set_style_text_color(sub, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 22); }
  lv_obj_add_event_cb(btn_lang, [](lv_event_t* e){ logSD("BTN: System -> Language"); showLanguageScreen(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* btn_upd = lv_btn_create(scr_system);
  lv_obj_set_size(btn_upd, BTN_W, BTN_H);
  lv_obj_set_pos(btn_upd, BTN_X, BTN_Y0 + BTN_H + BTN_GAP);
  lv_obj_set_style_bg_color(btn_upd, lv_color_hex(0x0a1a2a), 0);
  lv_obj_set_style_bg_color(btn_upd, lv_color_hex(0x1a2a40), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_upd, 10, 0);
  lv_obj_set_style_shadow_width(btn_upd, 0, 0);
  lv_obj_set_style_border_width(btn_upd, 1, 0);
  lv_obj_set_style_border_color(btn_upd, lv_color_hex(0x1a2a40), 0);
  { lv_obj_t* ico = lv_label_create(btn_upd);
    lv_label_set_text(ico, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_color(ico, lv_color_hex(0x28d49a), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_24, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t* lbl = lv_label_create(btn_upd);
    lv_label_set_text(lbl, T(STR_BTN_FW_UPDATE));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t* sub = lv_label_create(btn_upd);
    lv_label_set_text(sub, T(STR_BTN_FW_SUB));
    lv_obj_set_style_text_color(sub, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 22); }
  lv_obj_add_event_cb(btn_upd, [](lv_event_t* e){ logSD("BTN: -> OTA"); show_ota_pending = true; }, LV_EVENT_CLICKED, nullptr);

  lbl_fw_badge = lv_obj_create(scr_system);
  lv_obj_set_size(lbl_fw_badge, 14, 14);
  lv_obj_set_pos(lbl_fw_badge, 456, BTN_Y0 + BTN_H + BTN_GAP);
  lv_obj_set_style_radius(lbl_fw_badge, 7, 0);
  lv_obj_set_style_bg_color(lbl_fw_badge, lv_color_hex(0xe03030), 0);
  lv_obj_set_style_border_color(lbl_fw_badge, lv_color_hex(0x0a1020), 0);
  lv_obj_set_style_border_width(lbl_fw_badge, 2, 0);
  lv_obj_set_style_pad_all(lbl_fw_badge, 0, 0);
  lv_obj_clear_flag(lbl_fw_badge, LV_OBJ_FLAG_SCROLLABLE);
  if (!update_available) lv_obj_add_flag(lbl_fw_badge, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* btn_info = lv_btn_create(scr_system);
  lv_obj_set_size(btn_info, BTN_W, BTN_H);
  lv_obj_set_pos(btn_info, BTN_X, BTN_Y0 + 2*(BTN_H + BTN_GAP));
  lv_obj_set_style_bg_color(btn_info, lv_color_hex(0x0a1e30), 0);
  lv_obj_set_style_bg_color(btn_info, lv_color_hex(0x1a3050), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_info, 10, 0);
  lv_obj_set_style_shadow_width(btn_info, 0, 0);
  lv_obj_set_style_border_width(btn_info, 1, 0);
  lv_obj_set_style_border_color(btn_info, lv_color_hex(0x1a3050), 0);
  { lv_obj_t* ico = lv_label_create(btn_info);
    lv_label_set_text(ico, LV_SYMBOL_BELL);
    lv_obj_set_style_text_color(ico, lv_color_hex(0x28d49a), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_24, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t* lbl = lv_label_create(btn_info);
    lv_label_set_text(lbl, T(STR_BTN_INFO));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t* sub = lv_label_create(btn_info);
    lv_label_set_text(sub, T(STR_BTN_INFO_SUB));
    lv_obj_set_style_text_color(sub, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 22); }
  lv_obj_add_event_cb(btn_info, [](lv_event_t* e){ logSD("BTN: System -> Info"); show_info_pending = true; }, LV_EVENT_CLICKED, nullptr);

  int reset_y = BTN_Y0 + 3*(BTN_H + BTN_GAP);
  const int HALF_W = 220;

  lv_obj_t* btn_reset = lv_btn_create(scr_system);
  lv_obj_set_size(btn_reset, HALF_W, RESET_H);
  lv_obj_set_pos(btn_reset, BTN_X, reset_y);
  lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x0a1a2a), 0);
  lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x1a2a40), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_reset, 8, 0);
  lv_obj_set_style_shadow_width(btn_reset, 0, 0);
  lv_obj_set_style_border_width(btn_reset, 2, 0);
  lv_obj_set_style_border_color(btn_reset, lv_color_hex(0x3a1010), 0);
  { lv_obj_t* lbl = lv_label_create(btn_reset);
    char buf[32]; strncpy(buf, T(STR_BTN_FACTORY_RESET), sizeof(buf)-1);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xff6060), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -7);
    lv_obj_t* sub = lv_label_create(btn_reset);
    char sub_buf[48]; strncpy(sub_buf, T(STR_BTN_FACTORY_RESET_SUB), sizeof(sub_buf)-1);
    lv_label_set_text(sub, sub_buf);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x804040), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 9); }
  lv_obj_add_event_cb(btn_reset, [](lv_event_t* e) {
    lv_obj_t* pop = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pop, 480, 320);
    lv_obj_set_pos(pop, 0, 0);
    lv_obj_set_style_bg_color(pop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pop, LV_OPA_80, 0);
    lv_obj_set_style_border_width(pop, 0, 0);
    lv_obj_set_style_radius(pop, 0, 0);
    lv_obj_set_style_pad_all(pop, 0, 0);
    lv_obj_clear_flag(pop, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(pop);
    lv_obj_set_size(box, 440, 240);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1a0808), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x602020), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_t = lv_label_create(box);
    char buf_t[48]; strncpy(buf_t, T(STR_FACTORY_RESET_TITLE), sizeof(buf_t)-1);
    lv_label_set_text(lbl_t, buf_t);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0xff6060), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* lbl_m = lv_label_create(box);
    char buf_m[256]; strncpy(buf_m, T(STR_FACTORY_RESET_MSG), sizeof(buf_m)-1);
    lv_label_set_text(lbl_m, buf_m);
    lv_obj_set_style_text_color(lbl_m, lv_color_hex(0xc8d8f0), 0);
    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_align(lbl_m, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_m, 400);
    lv_obj_align(lbl_m, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t* btn_c = lv_btn_create(box);
    lv_obj_set_size(btn_c, 180, 44);
    lv_obj_set_pos(btn_c, 12, 184);
    lv_obj_set_style_bg_color(btn_c, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_color(btn_c, lv_color_hex(0x1a2840), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_c, 8, 0);
    lv_obj_set_style_shadow_width(btn_c, 0, 0);
    lv_obj_set_style_border_width(btn_c, 1, 0);
    lv_obj_set_style_border_color(btn_c, lv_color_hex(0x1a2840), 0);
    lv_obj_add_event_cb(btn_c, [](lv_event_t* e){
      lv_obj_del(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_c = lv_label_create(btn_c);
    char buf_c[32]; strncpy(buf_c, T(STR_CANCEL), sizeof(buf_c)-1);
    lv_label_set_text(lbl_c, buf_c);
    lv_obj_set_style_text_color(lbl_c, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_ext_14, 0);
    lv_obj_align(lbl_c, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* btn_ok = lv_btn_create(box);
    lv_obj_set_size(btn_ok, 228, 44);
    lv_obj_set_pos(btn_ok, 200, 184);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x3a1010), 0);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x602020), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_ok, 8, 0);
    lv_obj_set_style_shadow_width(btn_ok, 0, 0);
    lv_obj_set_style_border_width(btn_ok, 1, 0);
    lv_obj_set_style_border_color(btn_ok, lv_color_hex(0x602020), 0);
    lv_obj_add_event_cb(btn_ok, [](lv_event_t* e){
      logSD("Factory Reset: erasing NVS flash partition");
      Serial.println("Factory Reset: erasing NVS flash partition");
      if (sd_available) SD.end();
      delay(100);
      nvs_flash_erase();
      nvs_flash_init();
      delay(200);
      ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl_ok = lv_label_create(btn_ok);
    char buf_ok[48]; strncpy(buf_ok, T(STR_FACTORY_RESET_CONFIRM), sizeof(buf_ok)-1);
    lv_label_set_text(lbl_ok, buf_ok);
    lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0xff8080), 0);
    lv_obj_set_style_text_font(lbl_ok, &lv_font_montserrat_ext_14, 0);
    lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* btn_reboot = lv_btn_create(scr_system);
  lv_obj_set_size(btn_reboot, HALF_W, RESET_H);
  lv_obj_set_pos(btn_reboot, BTN_X + HALF_W + 16, reset_y);
  lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0x0a1a2a), 0);
  lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0x1a2a40), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_reboot, 8, 0);
  lv_obj_set_style_shadow_width(btn_reboot, 0, 0);
  lv_obj_set_style_border_width(btn_reboot, 1, 0);
  lv_obj_set_style_border_color(btn_reboot, lv_color_hex(0x1a2a40), 0);
  { lv_obj_t* lbl = lv_label_create(btn_reboot);
    char buf[32]; strncpy(buf, T(STR_BTN_REBOOT), sizeof(buf)-1);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xc8d8f0), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -7);
    lv_obj_t* sub = lv_label_create(btn_reboot);
    char sub_buf[32]; strncpy(sub_buf, T(STR_BTN_REBOOT_SUB), sizeof(sub_buf)-1);
    lv_label_set_text(sub, sub_buf);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x4a6fa0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 9); }
  lv_obj_add_event_cb(btn_reboot, [](lv_event_t* e) {
    logSD("BTN: System -> Reboot");
    if (sd_available) SD.end();
    delay(100);
    ESP.restart();
  }, LV_EVENT_CLICKED, nullptr);

  if (sd_verbose) logSD("[verbose] buildSystemScreen: done");
}