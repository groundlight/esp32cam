// Groundlight library for Arduino
// MIT License

#pragma once

#include "Arduino.h"
#include "WiFiClient.h"

#if __has_include("esp_camera.h")
  #include "esp_camera.h"
  #define HAS_ESP_CAMERA_LIB
#endif

#if __has_include("ArduinoJson.h")
  #include "ArduinoJson.h"
  #define HAS_JSON_LIB
#endif

#ifdef HAS_ESP_CAMERA_LIB
  String submit_image_query(camera_fb_t *image_bytes, char *endpoint, char *detector_id, char *api_token);
  String submit_image_query_with_client(camera_fb_t *image_bytes, const char *endpoint, char *detector_id, char *api_token, WiFiClient &client, int port);
#endif

String get_image_query(char *endpoint, const char *query_id, char *api_token);
bool adjust_confidence(const char *endpoint, const char *predictorId, float confidence, const char *apiToken);
String get_detectors(const char *endpoint, const char *apiToken);

#ifdef HAS_JSON_LIB
struct detector
{
  char id[40];
  char type[40];
  char created_at[60];
  char name[40];
  char query[40];
  char group_name[40];
  float confidence_threshold;
};

struct detector_list
{
  detector *detectors;
  uint size;
};

detector_list get_detector_list(const char *endpoint, const char *apiToken);
String detector_to_string(detector d);
detector get_detector_by_id(const char *endpoint, const char *detectorId, const char *apiToken);
float get_query_confidence(const String &jsonResults);
String get_query_id(const String &jsonResults);
#endif