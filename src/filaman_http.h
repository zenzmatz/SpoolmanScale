#pragma once

#include <Arduino.h>

String filamanErrorMessage(const String& response, int http_code);
bool filamanSendHeartbeat();
bool filamanConnectNow(String& status_detail);
bool filamanPostAuthorized(const char* path, const String& payload, int& http_code, String& response);
bool filamanGetAuthorized(const char* path, int& http_code, String& response);