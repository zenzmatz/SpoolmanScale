#include "backend_types.h"
#include "filaman_http.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

extern BackendMode cfg_backend_mode;
extern bool wifi_ok;
extern bool sd_verbose;
extern char cfg_filaman_url[];
extern char cfg_filaman_token[];
extern char cfg_filaman_code[];

void backendShowPendingStatus(const char* feature, const char* detail);
void saveFilamanToken(const char* token);
void saveFilamanCode(const char* code);
void logSDf(const char* fmt, ...);

namespace {

String filamanBaseUrl() {
  String base = String(cfg_filaman_url);
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

bool filamanBeginRequest(HTTPClient& http, WiFiClientSecure& secure, const String& url) {
  http.setTimeout(8000);
  if (url.startsWith("https://")) {
    secure.setInsecure();
    return http.begin(secure, url);
  }
  return http.begin(url);
}

bool ensureFilamanRegistration(bool notify_ui = true) {
  if (cfg_filaman_token[0]) return true;
  if (!cfg_filaman_url[0]) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", "Set the FilaMan server URL first.");
    return false;
  }
  if (!cfg_filaman_code[0]) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", "Set a device code or token first.");
    return false;
  }

  String base = filamanBaseUrl();
  if (!base.length()) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", "FilaMan URL is empty.");
    return false;
  }

  HTTPClient http;
  WiFiClientSecure secure;
  String url = base + "/api/v1/devices/register";
  if (!filamanBeginRequest(http, secure, url)) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", "Could not open the registration endpoint.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Code", cfg_filaman_code);
  int code = http.POST("{}");
  String response = http.getString();
  http.end();

  if (code != 200 && code != 201) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", filamanErrorMessage(response, code).c_str());
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", "Registration response could not be parsed.");
    return false;
  }

  const char* token = doc["token"] | "";
  if (!token[0]) {
    if (notify_ui) backendShowPendingStatus("FilaMan auth", "Registration response did not include a token.");
    return false;
  }

  saveFilamanToken(token);
  saveFilamanCode("");
  return true;
}

}  // namespace

String filamanErrorMessage(const String& response, int http_code) {
  JsonDocument doc;
  if (!deserializeJson(doc, response)) {
    if (doc["detail"].is<JsonArray>()) {
      const char* validation_msg = doc["detail"][0]["msg"] | "";
      if (validation_msg[0]) return String(validation_msg);
    }
    const char* nested = doc["detail"]["message"] | "";
    if (nested[0]) return String(nested);
    const char* direct = doc["message"] | "";
    if (direct[0]) return String(direct);
  }
  String fallback = String("HTTP ") + http_code;
  return fallback;
}

bool filamanSendHeartbeat() {
  if (cfg_backend_mode != BACKEND_FILAMAN) return false;
  if (!wifi_ok) return false;
  if (!cfg_filaman_url[0]) return false;
  if (!cfg_filaman_token[0] && !cfg_filaman_code[0]) return false;
  if (!ensureFilamanRegistration(false)) return false;

  String base = filamanBaseUrl();
  if (!base.length()) return false;

  HTTPClient http;
  WiFiClientSecure secure;
  String url = base + "/api/v1/devices/heartbeat";
  if (!filamanBeginRequest(http, secure, url)) return false;

  JsonDocument doc;
  doc["ip_address"] = WiFi.localIP().toString();

  String payload;
  serializeJson(doc, payload);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Device ") + cfg_filaman_token);
  int code = http.POST(payload);
  String response = http.getString();
  http.end();

  if (code != 200) {
    String detail = filamanErrorMessage(response, code);
    logSDf("FilaMan heartbeat failed: HTTP %d %s", code, detail.c_str());
    return false;
  }

  if (sd_verbose) logSDf("FilaMan heartbeat ok: %s", WiFi.localIP().toString().c_str());
  return true;
}

bool filamanConnectNow(String& status_detail) {
  status_detail = "";

  if (!wifi_ok) {
    status_detail = "WiFi is not connected.";
    backendShowPendingStatus("FilaMan auth", status_detail.c_str());
    return false;
  }

  if (!ensureFilamanRegistration(true)) {
    status_detail = cfg_filaman_token[0]
      ? "Stored credentials could not be verified."
      : "Device registration failed.";
    return false;
  }

  String base = filamanBaseUrl();
  if (!base.length()) {
    status_detail = "FilaMan URL is empty.";
    backendShowPendingStatus("FilaMan auth", status_detail.c_str());
    return false;
  }

  HTTPClient http;
  WiFiClientSecure secure;
  String url = base + "/api/v1/devices/heartbeat";
  if (!filamanBeginRequest(http, secure, url)) {
    status_detail = "Could not open the heartbeat endpoint.";
    backendShowPendingStatus("FilaMan auth", status_detail.c_str());
    return false;
  }

  JsonDocument doc;
  doc["ip_address"] = WiFi.localIP().toString();

  String payload;
  serializeJson(doc, payload);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Device ") + cfg_filaman_token);
  int code = http.POST(payload);
  String response = http.getString();
  http.end();

  if (code != 200) {
    status_detail = filamanErrorMessage(response, code);
    backendShowPendingStatus("FilaMan auth", status_detail.c_str());
    return false;
  }

  status_detail = cfg_filaman_token[0]
    ? "Connected to FilaMan. Device token is stored."
    : "Connected to FilaMan.";
  logSDf("FilaMan connect ok: %s", WiFi.localIP().toString().c_str());
  return true;
}

bool filamanPostAuthorized(const char* path, const String& payload, int& http_code, String& response) {
  response = "";
  http_code = 0;

  if (!wifi_ok) {
    backendShowPendingStatus("FilaMan request", "WiFi is not connected.");
    return false;
  }
  if (!ensureFilamanRegistration()) return false;

  String base = filamanBaseUrl();
  if (!base.length()) {
    backendShowPendingStatus("FilaMan request", "FilaMan URL is empty.");
    return false;
  }

  HTTPClient http;
  WiFiClientSecure secure;
  String url = base + path;
  if (!filamanBeginRequest(http, secure, url)) {
    backendShowPendingStatus("FilaMan request", "Could not open the API endpoint.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Device ") + cfg_filaman_token);
  http_code = http.POST(payload);
  response = http.getString();
  http.end();
  return true;
}

bool filamanGetAuthorized(const char* path, int& http_code, String& response) {
  response = "";
  http_code = 0;

  if (!wifi_ok) {
    backendShowPendingStatus("FilaMan request", "WiFi is not connected.");
    return false;
  }
  if (!ensureFilamanRegistration()) return false;

  String base = filamanBaseUrl();
  if (!base.length()) {
    backendShowPendingStatus("FilaMan request", "FilaMan URL is empty.");
    return false;
  }

  HTTPClient http;
  WiFiClientSecure secure;
  String url = base + path;
  if (!filamanBeginRequest(http, secure, url)) {
    backendShowPendingStatus("FilaMan request", "Could not open the API endpoint.");
    return false;
  }

  http.addHeader("Authorization", String("Device ") + cfg_filaman_token);
  http_code = http.GET();
  response = http.getString();
  http.end();
  return true;
}