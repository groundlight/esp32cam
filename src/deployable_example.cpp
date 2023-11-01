/*

Groundlight deployable example of image classification from a visual query. Provided under MIT License below:

Copyright (c) 2023 Groundlight, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <Arduino.h>
#include <Preferences.h>
#include "WiFi.h"
#include <esp_camera.h>
#include <time.h>
#include "ArduinoJson.h"
#include "groundlight.h"

#include "camera_pins.h" // thank you seeedstudio for this file
#include "integrations.h"
#include "stacklight.h"

#ifdef CAMERA_MODEL_M5STACK_PSRAM
  #define RESET_SETTINGS_GPIO         38
  #define RESET_SETTINGS_GPIO_DEFAULT LOW
  #include <Adafruit_NeoPixel.h>
  #define NEOPIXEL_PIN                G4 // (SDA) if doesn't work, try G13 (SCL)
  #define NEOPIXEL_COUNT              3
#else
  #define RESET_SETTINGS_GPIO         -1
  #define RESET_SETTINGS_GPIO_DEFAULT HIGH
#endif

#define FRAME_ARR_LEN 1280 * 1024 * 2 / 64
#define COLOR_VAL_MAX 31
uint8_t *frame_565;
uint8_t *frame_565_old;
#define ALPHA_DIVISOR 10
#define ALPHA FRAME_ARR_LEN / ALPHA_DIVISOR
#define BETA 20 // out of 31

enum QueryState {
  WAITING_TO_QUERY,
  DNS_NOT_FOUND,
  SSL_CONNECTION_FAILURE,
  LAST_RESPONSE_PASS,
  LAST_RESPONSE_FAIL,
  LAST_RESPONSE_UNSURE,
  NOT_AUTHENTICATED,
};

String queryStateToString (QueryState state) {
  switch (state) {
    case WAITING_TO_QUERY:
      return "WAITING_TO_QUERY";
    case SSL_CONNECTION_FAILURE:
      return "CONNECTION_FAILURE";
    case DNS_NOT_FOUND:
      return "DNS_NOT_FOUND";
    case LAST_RESPONSE_PASS:
      return "LAST_RESPONSE_PASS";
    case LAST_RESPONSE_FAIL:
      return "LAST_RESPONSE_FAIL";
    case LAST_RESPONSE_UNSURE:
      return "LAST_RESPONSE_UNSURE";
    case NOT_AUTHENTICATED:
      return "NOT_AUTHENTICATED";
    default:
      return "UNKNOWN";
  }
}

enum NotificationState {
  NOTIFICATION_NOT_ATTEMPTED,
  NOTIFICATIONS_SENT,
  NOTIFICATION_ATTEMPT_FAILED,
};

String notificationStateToString (NotificationState state) {
  switch (state) {
    case NOTIFICATION_NOT_ATTEMPTED:
      return "NOTIFICATION_NOT_ATTEMPTED";
    case NOTIFICATIONS_SENT:
      return "NOTIFICATIONS_SENT";
    case NOTIFICATION_ATTEMPT_FAILED:
      return "NOTIFICATION_ATTEMPT_FAILED";
    default:
      return "UNKNOWN";
  }
}

enum StacklightState {
  STACKLIGHT_NOT_FOUND,
  STACKLIGHT_ONLINE,
  STACKLIGHT_PAIRED,
};

String stacklightStateToString (StacklightState state) {
  switch (state) {
    case STACKLIGHT_NOT_FOUND:
      return "STACKLIGHT_NOT_FOUND";
    case STACKLIGHT_ONLINE:
      return "STACKLIGHT_ONLINE";
    case STACKLIGHT_PAIRED:
      return "STACKLIGHT_PAIRED";
    default:
      return "UNKNOWN";
  }
}

QueryState queryState = WAITING_TO_QUERY;
NotificationState notificationState = NOTIFICATION_NOT_ATTEMPTED;
StacklightState stacklightState = STACKLIGHT_NOT_FOUND;

camera_fb_t *frame = NULL;
int *last_frame_buffer = NULL;
char groundlight_endpoint[60] = "api.groundlight.ai";

#ifdef ENABLE_AP
  #include "ap_configuration.h"
#else
  Preferences preferences;
#endif

const bool SHOW_LOGS = false;
void debug(String message) {
  if (SHOW_LOGS) {
    Serial.println(message);
  }
}
void debug(float message) {
  if (SHOW_LOGS) {
    Serial.println(message);
  }
}

char groundlight_API_key[75];
char groundlight_det_id[100];
char groundlight_det_name[100];
char ssid[100];
char password[100];
int query_delay = 30; // 30 seconds
int start_hr = 8;
int end_hr = 17;
bool should_deep_sleep = false;
float targetConfidence = 0.9;
int retryLimit = 10;

String queryResults = "NONE_YET";
char last_label[30] = "NONE_YET";
bool wifi_connected = false;

String input = "";
int last_upload_time = 0;
int last_print_time = 0;
char input2[1000];
int input2_index = 0;
bool new_data = false;

StaticJsonDocument<1024> resultDoc;
StaticJsonDocument<1024> synthesisDoc;

#ifdef NEOPIXEL_PIN
  Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

  void pixelControllerTask(void * parameter) {
    bool was_on = false;
    int delay_ms = 200;
    int flash_delay_ms = 1000;
    int inter_per_flash = flash_delay_ms / delay_ms;
    while (true) {
      for (int i = 0; i < inter_per_flash; i++) {
        String label = last_label;
        label.toUpperCase();
        preferences.begin("config");
        QueryState _queryState = (QueryState) preferences.getInt("qSt", queryState);
        preferences.end();
        if ((_queryState == QueryState::DNS_NOT_FOUND || _queryState == QueryState::SSL_CONNECTION_FAILURE || _queryState == QueryState::NOT_AUTHENTICATED) && i == 0) {
          if (was_on) {
            was_on = false;
            pixels.setPixelColor(0, pixels.Color(0, 0, 0));
            pixels.setPixelColor(1, pixels.Color(0, 0, 0));
            pixels.setPixelColor(2, pixels.Color(0, 0, 0));
          } else {
            was_on = true;
            pixels.setPixelColor(0, pixels.Color(50, 0, 0));
            pixels.setPixelColor(1, pixels.Color(50, 0, 0));
            pixels.setPixelColor(2, pixels.Color(50, 0, 0));
          }
        } else if (label == "PASS" || label == "YES") {
          pixels.setPixelColor(0, pixels.Color(0, 100, 0));
          pixels.setPixelColor(1, pixels.Color(0, 0, 0));
          pixels.setPixelColor(2, pixels.Color(0, 0, 0));
        } else if (label == "FAIL" || label == "NO") {
          pixels.setPixelColor(0, pixels.Color(0, 0, 0));
          pixels.setPixelColor(1, pixels.Color(0, 0, 0));
          pixels.setPixelColor(2, pixels.Color(100, 0, 0));
        } else if (label == "UNSURE" || label == "__UNSURE") {
          pixels.setPixelColor(0, pixels.Color(0, 0, 0));
          pixels.setPixelColor(1, pixels.Color(100, 50, 0));
          pixels.setPixelColor(2, pixels.Color(0, 0, 0));
        } else if (i == 0) {
          if (was_on) {
            was_on = false;
            pixels.setPixelColor(0, pixels.Color(0, 0, 0));
            pixels.setPixelColor(1, pixels.Color(0, 0, 0));
            pixels.setPixelColor(2, pixels.Color(0, 0, 0));
          } else {
            was_on = true;
            pixels.setPixelColor(0, pixels.Color(0, 0, 0));
            pixels.setPixelColor(1, pixels.Color(100, 50, 0));
            pixels.setPixelColor(2, pixels.Color(0, 0, 0));
          }
        }
        pixels.show();
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
      }
    }
  }
#endif

void listener(void * parameter);

bool try_save_config(char * input);
void try_answer_query(String input);
bool is_motion_detected(camera_fb_t* frame, int alpha, int beta);

void printInfo();
bool shouldDoNotification(String queryRes);
bool sendNotifications(char *label, camera_fb_t *fb);
bool notifyStacklight(const char * label);
bool decodeWorkingHoursString(String working_hours);
void deep_sleep() {
  int time_elapsed = millis() - last_upload_time;
  int time_to_sleep = (query_delay - 10) * 1000000 - (1000 * time_elapsed);
  esp_sleep_enable_timer_wakeup(time_to_sleep);
  esp_deep_sleep_start();
}

void setup() {

#if defined(GPIO_LED_FLASH)
  pinMode(GPIO_LED_FLASH, OUTPUT);
  digitalWrite(GPIO_LED_FLASH, LOW);
#endif
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
#endif
  Serial.begin(115200);
  Serial.println("Edgelight waking up...");
  xTaskCreate(
    listener,         // Function that should be called
    "Uart Listener",  // Name of the task (for debugging)
    10000,            // Stack size (bytes)
    NULL,             // Parameter to pass
    1,                // Task priority
    NULL              // Task handle
  );
  if (RESET_SETTINGS_GPIO != -1) {
    if (RESET_SETTINGS_GPIO_DEFAULT == LOW) {
      pinMode(RESET_SETTINGS_GPIO, INPUT_PULLDOWN);
    } else {
      pinMode(RESET_SETTINGS_GPIO, INPUT_PULLUP);
    }
    if (digitalRead(RESET_SETTINGS_GPIO) != RESET_SETTINGS_GPIO_DEFAULT) {
      preferences.begin("config", false);
      preferences.clear();
      preferences.end();
      delay(500);
      debug("Reset settings!");
    }
  }
#ifdef NEOPIXEL_PIN
  pixels.begin();

  xTaskCreate(
    pixelControllerTask,         // Function that should be called
    "Pixel Controller",  // Name of the task (for debugging)
    5000,            // Stack size (bytes)
    NULL,             // Parameter to pass
    1,                // Task priority
    NULL              // Task handle
  );
#endif
#ifdef ENABLE_AP
  WiFi.mode(WIFI_AP_STA);
  // last 6 digits from mac address
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac = mac.substring(6);
  preferences.begin("config", false);
  ap_setup((StringSumHelper) "ESP32-CAM-" + mac, preferences.getString("ap_pw", ""));
  preferences.end();
#endif
  preferences.begin("config", false);
  if (preferences.isKey("ssid") && preferences.isKey("password") && preferences.isKey("api_key") && preferences.isKey("det_id") && preferences.isKey("query_delay")) {
    preferences.getString("ssid", ssid, 100);
    preferences.getString("password", password, 100);
    preferences.getString("api_key", groundlight_API_key, 75);
    preferences.getString("det_id", groundlight_det_id, 100);
    query_delay = preferences.getInt("query_delay", query_delay);
    if (query_delay > 30) {
      should_deep_sleep = true;
    }
    WiFi.begin(ssid, password);
    wifi_connected = true;
  }
  if (preferences.isKey("ssid") && preferences.isKey("sl_uuid") && !preferences.isKey("sl_ip")) {
    debug("Initializing Stacklight through AP");
    if (Stacklight::isStacklightAPAvailable(preferences.getString("sl_uuid", ""))) {
      String SSID = ((const StringSumHelper)"GL_STACKLIGHT_" + preferences.getString("sl_uuid", ""));
      WiFi.begin(SSID, (const StringSumHelper)"gl_stacklight_password_" + preferences.getString("sl_uuid", ""));
      for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
      }
      if(WiFi.isConnected()) {
        String res = Stacklight::initStacklight(ssid, password);
        if (res != "") {
          preferences.putString("sl_ip", res);
          stacklightState = STACKLIGHT_PAIRED;
        }
      } else {
        debug("Couldn't connect to stacklight");
      }
      WiFi.disconnect();
    } else {
      debug("Couldn't find stacklight");
    }

    WiFi.begin(ssid, password);
  }
  if (preferences.isKey("endpoint") && preferences.getString("endpoint", "") != "") {
    preferences.getString("endpoint", groundlight_endpoint, 60);
  }
  if (preferences.isKey("tConf")) {
    targetConfidence = preferences.getFloat("tConf", targetConfidence);
  }
  if (preferences.isKey("waitTime")) {
    retryLimit = preferences.getInt("waitTime", retryLimit);
  }
  if (preferences.isKey("det_name")) {
    preferences.getString("det_name", groundlight_det_name, 100);
  }
  if (preferences.isKey("wkhrs")) {
    decodeWorkingHoursString(preferences.getString("wkhrs", ""));
  }
  preferences.end();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Photo Quality Settings
  // config.frame_size = FRAMESIZE_UXGA; // See here for a list of options and resolutions: https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h#L84
  config.frame_size = FRAMESIZE_SXGA; // See here for a list of options and resolutions: https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h#L84
  config.jpeg_quality = 10;           // lower means higher quality
  config.fb_count = 1;

  esp_err_t error_code = esp_camera_init(&config);
  if (error_code != ESP_OK)
  {
    delay(3000);
    ESP.restart(); // some boards are less reliable for initialization and will everntually just start working
    return;
  }
  
  sensor_t * s = esp_camera_sensor_get();
  preferences.begin("config", false);
  if (!preferences.getBool("flip_vert", false)) s->set_vflip(s, 1);
  if (preferences.getBool("flip_hori", false)) s->set_hmirror(s, 1);
  preferences.end();

  // alloc memory for 565 frames
  frame_565 = (uint8_t *) ps_malloc(FRAME_ARR_LEN);
  frame_565_old = (uint8_t *) ps_malloc(FRAME_ARR_LEN);

  debug((StringSumHelper) "using detector  : " + groundlight_det_id);
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, LOW);
#endif
}

void listener(void * parameter) {
  char input3[1000];
  int input3_index = 0;
  bool new_data_ = false;
  while (true) {
    while (Serial.available() > 0 && new_data_ == false) {
      input3[input3_index] = Serial.read();
      if (input3[input3_index] == '\n') {
        input3[input3_index] = '\0';
        new_data_ = true;
      }
      input3_index++;
      break;
    }
    
    if (new_data_) {
      debug("New data");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      if (((String) input3).indexOf("query") != -1 && ((String) input3).indexOf("ssid") == -1) {
        try_answer_query(input3);
      } else if(try_save_config(input3)) {
        debug("Saved config!");
        WiFi.begin(ssid, password);
        wifi_connected = true;
      }
      input = "";
      input3_index = 0;
      new_data_ = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void loop () {
  if (!wifi_connected) {
    if (millis() > last_print_time + 1000) {
      debug("Waiting for Credentials...");
      last_print_time = millis();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
  } else if (millis() > last_print_time + 1000) {
    last_print_time = millis();
  }

  if (should_deep_sleep) {
    int time = millis();
    // make sure we don't startup in less than 10 seconds
    if (time < 10000) {
      vTaskDelay((10000 - time) / portTICK_PERIOD_MS);
    } else {
      debug("Took too long to startup!");
    }
  }

  if (millis() < last_upload_time + query_delay * 1000 && !should_deep_sleep) {
    return;
  } else {
    last_upload_time = millis();
    debug("Taking a lap!");
  }

  preferences.begin("config", true);
  if (preferences.isKey("wkhrs") && preferences.getString("wkhrs", "") != "") {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)){
      debug("Failed to obtain time");
    } else {
      int hour = timeinfo.tm_hour;
      int minute = timeinfo.tm_min;
      bool in_working_hours = false;
      if (start_hr < end_hr) {
        in_working_hours = hour >= start_hr && hour < end_hr;
      } else { // start_hr > end_hr
        in_working_hours = hour >= start_hr || hour < end_hr;
      }
      if (!in_working_hours) {
        debug("Not in working hours!");
        last_upload_time = millis();
        preferences.end();
        if (should_deep_sleep) {
          deep_sleep();
        }
        return;
      }
    }
  }
  preferences.end();

  // get image from camera into a buffer
  #if defined(GPIO_LED_FLASH)
    digitalWrite(GPIO_LED_FLASH, HIGH);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  #endif

  frame = esp_camera_fb_get();
  // Testing how to get the latest image
  esp_camera_fb_return(frame);
  frame = esp_camera_fb_get();

  #if defined(GPIO_LED_FLASH)
    digitalWrite(GPIO_LED_FLASH, LOW);
  #endif

  if (!frame)
  {
    debug("Camera capture failed! Restarting system!");
    delay(3000);
    ESP.restart(); // maybe this will fix things? hard to say. its not going to be worse
  }

  debug((StringSumHelper) "Captured image. Encoded size is " + frame->len + " bytes");

  preferences.begin("config");
  if (preferences.isKey("motion") && preferences.getBool("motion") && preferences.isKey("mot_a") && preferences.isKey("mot_b")) {
    // if (is_motion_detected(frame, ALPHA, BETA)) {
    int alpha = round(preferences.getString("mot_a", "0.0").toFloat() * (float) FRAME_ARR_LEN);
    int beta = round(preferences.getString("mot_b", "0.0").toFloat() * (float) COLOR_VAL_MAX);
    if (is_motion_detected(frame, alpha, beta)) {
      debug("Motion detected!");
    } else {
      esp_camera_fb_return(frame);
      if (should_deep_sleep) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        deep_sleep();
      } else {
        return;
      }
    }
  }
  preferences.end();

  // wait for wifi connection
  if (!WiFi.isConnected()) {
    debug("Waiting for wifi connection...");
    for (int i = 0; i < 100 && !WiFi.isConnected(); i++) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }

  // could be '{"detail":"Not found."}'
  queryResults = submit_image_query(frame, groundlight_endpoint, groundlight_det_id, groundlight_API_key);
  String queryId = get_query_id(queryResults);

  debug("Query ID:");
  debug(queryId);
  debug("Confidence:");
  debug(get_query_confidence(queryResults));

  if (queryId == "NONE" || queryId == "") {
    debug("Failed to get query ID");
    esp_camera_fb_return(frame);
    return;
  }

  // wait for confident answer
  int currTime = millis();
  while (get_query_confidence(queryResults) < targetConfidence) {
    debug("Waiting for confident answer...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    queryResults = get_image_query(groundlight_endpoint, queryId.c_str(), groundlight_API_key);

    if (millis() > currTime + retryLimit * 1000) {
      debug("Retry limit reached!");
      break;
    }
  }
  ArduinoJson::DeserializationError error = deserializeJson(resultDoc, queryResults);
  if (error == ArduinoJson::DeserializationError::Ok) {
    if (resultDoc.containsKey("result")) {
      if (resultDoc["result"].containsKey("label")) {
        String label = resultDoc["result"]["label"].as<String>();
        label.toUpperCase();
        if (label == "QUERY_FAIL" && resultDoc["result"].containsKey("failure_reason")) {
          String reason = resultDoc["result"]["failure_reason"].as<String>();
          reason.toUpperCase();
          if (reason == "INITIAL_SSL_CONNECTION_FAILURE") {
            // DNS NOT FOUND
            queryState = QueryState::DNS_NOT_FOUND;
          } else if (reason == "SSL_CONNECTION_FAILURE") {
            // SSL CONNECTION FAILURE
            queryState = QueryState::SSL_CONNECTION_FAILURE;
          } else if (reason == "SSL_CONNECTION_FAILURE_COLLECTING_RESPONSE") {
            // SSL CONNECTION FAILURE
            queryState = QueryState::SSL_CONNECTION_FAILURE;
          } else if (reason == "NOT_AUTHENTICATED") {
            queryState = QueryState::NOT_AUTHENTICATED;
          }
        } else if (label != "QUERY_FAIL") {
          if (label == "PASS" || label == "YES") {
            queryState = LAST_RESPONSE_PASS;
          } else if (label == "FAIL" || label == "NO") {
            queryState = LAST_RESPONSE_FAIL;
          } else if (label == "UNSURE" || label == "__UNSURE") {
            queryState = LAST_RESPONSE_UNSURE;
          }
        }
        preferences.begin("config");
        preferences.putInt("qSt", queryState);
        preferences.end();
      }
    }
    if (shouldDoNotification(queryResults)) {
      if (sendNotifications(last_label, frame)) {
        notificationState = NOTIFICATIONS_SENT;
      } else {
        notificationState = NOTIFICATION_ATTEMPT_FAILED;
      }
    }
    preferences.begin("config");
    if (preferences.isKey("sl_uuid")) {
      if (!notifyStacklight(last_label)) {
        debug("Failed to notify stacklight");
      }
    }
    resultDoc.clear();
    preferences.end();
    if (WiFi.SSID() != ssid) {
      WiFi.disconnect();
      vTaskDelay(500 / portTICK_PERIOD_MS);
      WiFi.begin(ssid, password);
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
  } else {
    debug("Failed to parse query results");
    debug(error.c_str());
  }

  esp_camera_fb_return(frame);

  if (should_deep_sleep) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    deep_sleep();
  }

  debug((StringSumHelper) "waiting " + query_delay + "s between queries...");
}

StaticJsonDocument<4096> doc;

bool try_save_config(char * input) {

  ArduinoJson::DeserializationError err = deserializeJson(doc, input);

  if (err.code() != ArduinoJson::DeserializationError::Ok) {
    debug("Failed to parse input as JSON");
    debug(err.c_str());
    return false;
  }

  if (!doc.containsKey("ssid") || !doc.containsKey("password") || !doc.containsKey("api_key") || !doc.containsKey("det_id") || !doc.containsKey("cycle_time")) {
    debug("Missing required fields");
    return false;
  }

  preferences.begin("config", false);
  preferences.putString("ssid", (const char *)doc["ssid"]);
  preferences.putString("password", (const char *)doc["password"]);
  preferences.putString("api_key", (const char *)doc["api_key"]);
  preferences.putString("det_id", (const char *)doc["det_id"]);
  preferences.putInt("query_delay", doc["cycle_time"]);
  if (doc.containsKey("det_name")) {
    preferences.putString("det_name", (const char *)doc["det_name"]);
  }
  if (doc.containsKey("query")) {
    preferences.putString("det_query", (const char *)doc["query"]);
  }
  if (doc.containsKey("endpoint") && doc["endpoint"] != "") {
    preferences.putString("endpoint", (const char *)doc["endpoint"]);
    strcpy(groundlight_endpoint, (const char *)doc["endpoint"]);
  } else {
    preferences.remove("endpoint");
  }
  if (doc.containsKey("ap_password")) {
    preferences.putString("ap_pw", (const char *)doc["ap_password"]);
  }
  if (doc.containsKey("waitTime")) {
    preferences.putInt("waitTime", doc["waitTime"]);
    retryLimit = doc["waitTime"];
  } else {
    preferences.remove("waitTime");
  }
  if (doc.containsKey("det_id") && doc.containsKey("api_key")) {
    detector det = get_detector_by_id(groundlight_endpoint, (const char *)doc["det_id"], (const char *)doc["api_key"]);
    preferences.putString("det_name", det.name);
    preferences.putString("det_query", det.query);
  }
  if (doc.containsKey("additional_config")) {
    debug("Found additional config!");
    if (doc["additional_config"].containsKey("target_confidence")) {
      debug("Found target confidence!");
      String targetConfidenceString = (const char *)doc["additional_config"]["target_confidence"];
      debug(targetConfidenceString);
      targetConfidence = targetConfidenceString.toFloat();
      preferences.putFloat("tConf", targetConfidence);
      preferences.putString("tCStr", targetConfidenceString);
    } else {
      preferences.remove("tConf");
    }
    if (doc["additional_config"].containsKey("endpoint") && doc["additional_config"]["endpoint"] != "") {
      preferences.putString("endpoint", (const char *)doc["additional_config"]["endpoint"]);
      strcpy(groundlight_endpoint, (const char *)doc["additional_config"]["endpoint"]);
    } else {
      preferences.remove("endpoint");
    }
    if (doc["additional_config"].containsKey("notificationOptions") && doc["additional_config"]["notificationOptions"] != "None") {
      debug("Found notification options!");
      preferences.putString("notiOptns", (const char *)doc["additional_config"]["notificationOptions"]);  
      if (doc["additional_config"].containsKey("slack") && doc["additional_config"]["slack"].containsKey("slackKey")) {
        debug("Found slack!");
        preferences.putString("slackKey", (const char *)doc["additional_config"]["slack"]["slackKey"]);
        preferences.putString("slackEndpoint", (const char *)doc["additional_config"]["slack"]["slackEndpoint"]);
      } else {
        preferences.remove("slackKey");
        preferences.remove("slackEndpoint");
      }
      if (doc["additional_config"].containsKey("twilio") && doc["additional_config"]["twilio"].containsKey("twilioKey")) {
        debug("Found twilio!");
        preferences.putString("twilioSID", (const char *)doc["additional_config"]["twilio"]["twilioSID"]);
        preferences.putString("twilioKey", (const char *)doc["additional_config"]["twilio"]["twilioKey"]);
        preferences.putString("twilioNumber", (const char *)doc["additional_config"]["twilio"]["twilioNumber"]);
        preferences.putString("twilioEndpoint", (const char *)doc["additional_config"]["twilio"]["twilioEndpoint"]);
      } else {
        preferences.remove("twilioKey");
      }
      if (doc["additional_config"].containsKey("email") && doc["additional_config"]["email"].containsKey("emailKey")) {
        debug("Found email!");
        preferences.putString("emailKey", (const char *)doc["additional_config"]["email"]["emailKey"]);
        preferences.putString("emailEndpoint", (const char *)doc["additional_config"]["email"]["emailEndpoint"]);
        preferences.putString("email", (const char *)doc["additional_config"]["email"]["email"]);
        preferences.putString("emailHost", (const char *)doc["additional_config"]["email"]["emailHost"]);
      } else {
        preferences.remove("emailKey");
      }
    }
    if (doc["additional_config"].containsKey("stacklight") && doc["additional_config"]["stacklight"].containsKey("uuid")) {
      debug("Found stacklight!");
      preferences.putString("sl_uuid", (const char *)doc["additional_config"]["stacklight"]["uuid"]);
      if (doc["additional_config"]["stacklight"].containsKey("switchColors")) {
        preferences.putBool("sl_switch", doc["additional_config"]["stacklight"]["switchColors"]);
      } else {
        preferences.remove("sl_switch");
      }
    } else {
      preferences.remove("sl_uuid");
    }
    if (doc["additional_config"].containsKey("working_hours")) {
      debug("Has working hours!");
      preferences.putString("wkhrs", (const char *)doc["additional_config"]["working_hours"]);
    } else {
      preferences.remove("wkhrs");
    }
    if (doc["additional_config"].containsKey("motion_detection")) {
      debug("Has motion detection!");
      preferences.putBool("motion", true);
      if (doc["additional_config"]["motion_detection"].containsKey("alpha")) {
        debug("Has alpha!");
        preferences.putString("mot_a",(const char *) doc["additional_config"]["motion_detection"]["alpha"]);
        preferences.putString("mot_b",(const char *) doc["additional_config"]["motion_detection"]["beta"]);
      } else {
        preferences.remove("mot_a");
        preferences.remove("mot_b");
      }
    } else {
      preferences.remove("motion");
    }
    if (doc["additional_config"].containsKey("flip_vert")) {
      debug("Has flip vert!");
      preferences.putBool("flip_vert", doc["additional_config"]["flip_vert"]);
      sensor_t * s = esp_camera_sensor_get();
      s->set_vflip(s, 0);
    } else {
      preferences.remove("flip_vert");
      sensor_t * s = esp_camera_sensor_get();
      s->set_vflip(s, 1);
    }
    if (doc["additional_config"].containsKey("flip_hori")) {
      debug("Has flip hori!");
      preferences.putBool("flip_hori", doc["additional_config"]["flip_hori"]);
      sensor_t * s = esp_camera_sensor_get();
      s->set_hmirror(s, 1);
    } else {
      preferences.remove("flip_hori");
      sensor_t * s = esp_camera_sensor_get();
      s->set_hmirror(s, 0);
    }
  }
  preferences.end();

  vTaskDelay(100 / portTICK_PERIOD_MS);
  Serial.println("doc Info:");
  serializeJson(doc, Serial);
  Serial.println();
  vTaskDelay(100 / portTICK_PERIOD_MS);

  strcpy(ssid, (const char *)doc["ssid"]);
  strcpy(password, (const char *)doc["password"]);
  strcpy(groundlight_API_key, (const char *)doc["api_key"]);
  strcpy(groundlight_det_id, (const char *)doc["det_id"]);
  query_delay = doc["cycle_time"];

  preferences.begin("config", true);
  decodeWorkingHoursString(preferences.getString("wkhrs", ""));
  preferences.end();

  doc.clear();

  return true;
}

bool shouldDoNotification(String queryRes) {
  if (queryRes == "\nNot authenticated.") {
    strcpy(last_label, "QUERY_FAIL");
    return false;
  }
  bool res = false;
  preferences.begin("config", true);
  if (preferences.isKey("notiOptns") && resultDoc["result"]["label"] != "QUERY_FAIL") {
    String notiOptns = preferences.getString("notiOptns", "None");
    if (notiOptns == "None") {
      // do nothing
    } else if (notiOptns == "On Change") {
      if (resultDoc["result"]["label"] != last_label) {
        res = true;
      }
    } else if (notiOptns == "On No/Fail") {
      if (resultDoc["result"]["label"] == "NO" || resultDoc["result"]["label"] == "FAIL") {
        res = true;
      }
    } else if (notiOptns == "On Yes/Pass") {
      if (resultDoc["result"]["label"] == "YES" || resultDoc["result"]["label"] == "PASS") {
        res = true;
      }
    }
  }
  strcpy(last_label, resultDoc["result"]["label"]);
  preferences.end();
  return res;
}

bool sendNotifications(char *label, camera_fb_t *fb) {
  preferences.begin("config");
  String det_name = preferences.getString("det_name", "");
  String det_query = preferences.getString("det_query", "");
  bool worked = true;
  if (det_name == "NONE") {
    detector det = get_detector_by_id(groundlight_endpoint, groundlight_det_id, groundlight_API_key);
    det_name = det.name;
    det_query = det.query;
    preferences.putString("det_name", det_name);
    preferences.putString("det_query", det_query);
  }
  if (preferences.isKey("slackKey") && preferences.isKey("slackEndpoint")) {
    debug("Sending Slack notification...");
    String slackKey = preferences.getString("slackKey", "");
    String slackEndpoint = preferences.getString("slackEndpoint", "");
    worked = worked && sendSlackNotification(det_name, det_query, slackKey, slackEndpoint, label, fb) == SlackNotificationResult::SUCCESS;
  }
  if (preferences.isKey("twilioKey") && preferences.isKey("twilioNumber") && preferences.isKey("twilioEndpoint")) {
    debug("Sending Twilio notification...");
    String twilioSID = preferences.getString("twilioSID", "");
    String twilioKey = preferences.getString("twilioKey", "");
    String twilioNumber = preferences.getString("twilioNumber", "");
    String twilioEndpoint = preferences.getString("twilioEndpoint", "");
    worked = worked && sendTwilioNotification(det_name, det_query, twilioSID, twilioKey, twilioNumber, twilioEndpoint, label, fb) == TwilioNotificationResult::SUCCESS;
  }
  if (preferences.isKey("emailKey") && preferences.isKey("email") && preferences.isKey("emailEndpoint")) {
    debug("Sending Email notification...");
    String emailKey = preferences.getString("emailKey", "");
    String email = preferences.getString("email", "");
    String emailEndpoint = preferences.getString("emailEndpoint", "");
    String host = preferences.getString("emailHost", "");
    worked = worked && sendEmailNotification(det_name, det_query, emailKey, email, emailEndpoint, host, label, fb) == EmailNotificationResult::SUCCESS;
  }
  preferences.end();
  return worked;
}

bool notifyStacklight(const char * label) {
  preferences.begin("config");
  if (!preferences.isKey("sl_uuid")) {
    preferences.end();
    return false;
  }

  if (preferences.isKey("sl_ip") && Stacklight::pushLabelToStacklight(preferences.getString("sl_ip", "").c_str(), label, preferences.getBool("sl_switch", false))) {
    preferences.end();
    return true;
  }

  debug("Connecting to Stacklight AP");
  if (Stacklight::isStacklightAPAvailable(preferences.getString("sl_uuid", ""))) {
    stacklightState = STACKLIGHT_ONLINE;
    String SSID = ((const StringSumHelper)"GL_STACKLIGHT_" + preferences.getString("sl_uuid", ""));
    WiFi.begin(SSID, (const StringSumHelper)"gl_stacklight_password_" + preferences.getString("sl_uuid", ""));
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    if(WiFi.isConnected()) {
      String res = Stacklight::tryConnectToStacklight(ssid, password);
      if (res != "") {
        preferences.putString("sl_ip", res);
        stacklightState = STACKLIGHT_PAIRED;
      }
    } else {
      debug("Couldn't connect to stacklight");
      return false;
    }
  } else {
    debug("Couldn't find stacklight");
    stacklightState = STACKLIGHT_NOT_FOUND;
    return false;
  }

  bool isKey = preferences.isKey("sl_ip");
  preferences.end();
  return isKey;
}

bool decodeWorkingHoursString(String working_hours) {
  // 08:17
  if (working_hours.length() != 5) {
    return false;
  }
  start_hr = working_hours.substring(0, 2).toInt();
  end_hr = working_hours.substring(3, 5).toInt();
  return true;
}

void try_answer_query(String input) {
  should_deep_sleep = false;
  if (input.indexOf("device_type") != -1) {
    Serial.println("Device Info:");
    Serial.println((StringSumHelper)"{\"name\":\"" + (String) NAME + "\",\"type\":\"Camera\",\"version\":\"" + VERSION + "\",\"mac_address\":\"" + WiFi.macAddress() + "\"}");
  } else if (input.indexOf("config") != -1) {
    preferences.begin("config");
    synthesisDoc["ssid"] = preferences.getString("ssid");
    synthesisDoc["password"] = preferences.getString("password");
    synthesisDoc["api_key"] = preferences.getString("api_key");
    synthesisDoc["det_id"] = preferences.getString("det_id");
    synthesisDoc["cycle_time"] = preferences.getInt("query_delay", query_delay);
    if (preferences.isKey("det_name")) {
      synthesisDoc["det_name"] = preferences.getString("det_name", "None");
    }
    if (preferences.isKey("det_query")) {
      synthesisDoc["det_query"] = preferences.getString("det_query", "None");
    }
    if (preferences.isKey("endpoint")) {
      synthesisDoc["additional_config"]["endpoint"] = preferences.getString("endpoint", "api.groundlight.ai");
    }
    if (preferences.isKey("tConf")) {
      synthesisDoc["additional_config"]["target_confidence"] = preferences.getString("tCStr", "0.9");
    }
    if (preferences.isKey("waitTime")) {
      synthesisDoc["waitTime"] = preferences.getInt("waitTime", retryLimit);
    }
    if (preferences.isKey("notiOptns")) {
      synthesisDoc["additional_config"]["notificationOptions"] = preferences.getString("notiOptns", "None");
    }
    if (preferences.isKey("slackKey") && preferences.isKey("slackEndpoint")) {
      synthesisDoc["additional_config"]["slack"]["slackKey"] = preferences.getString("slackKey", "None");
      synthesisDoc["additional_config"]["slack"]["slackEndpoint"] = preferences.getString("slackEndpoint", "None");
    }
    if (preferences.isKey("twilioSID") && preferences.isKey("twilioKey") && preferences.isKey("twilioNumber") && preferences.isKey("twilioEndpoint")) {
      synthesisDoc["additional_config"]["twilio"]["twilioSID"] = preferences.getString("twilioSID", "None");
      synthesisDoc["additional_config"]["twilio"]["twilioKey"] = preferences.getString("twilioKey", "None");
      synthesisDoc["additional_config"]["twilio"]["twilioNumber"] = preferences.getString("twilioNumber", "None");
      synthesisDoc["additional_config"]["twilio"]["twilioEndpoint"] = preferences.getString("twilioEndpoint", "None");
    }
    if (preferences.isKey("emailKey") && preferences.isKey("email") && preferences.isKey("emailEndpoint")) {
      synthesisDoc["additional_config"]["email"]["emailKey"] = preferences.getString("emailKey", "None");
      synthesisDoc["additional_config"]["email"]["emailEndpoint"] = preferences.getString("emailEndpoint", "None");
      synthesisDoc["additional_config"]["email"]["email"] = preferences.getString("email", "None");
      synthesisDoc["additional_config"]["email"]["emailHost"] = preferences.getString("emailHost", "None");
    }
    if (preferences.isKey("sl_uuid")) {
      synthesisDoc["additional_config"]["stacklight"]["uuid"] = preferences.getString("sl_uuid", "None");
      if (preferences.isKey("sl_switch")) {
        synthesisDoc["additional_config"]["stacklight"]["switchColors"] = preferences.getBool("sl_switch", false);
      }
    }
    if (preferences.isKey("wkhrs")) {
      synthesisDoc["additional_config"]["working_hours"] = preferences.getString("wkhrs", "None");
    }
    if (preferences.isKey("motion") && preferences.getBool("motion", false) && preferences.isKey("mot_a") && preferences.isKey("mot_b")) {
      synthesisDoc["additional_config"]["motion_detection"]["alpha"] = preferences.getString("mot_a");
      synthesisDoc["additional_config"]["motion_detection"]["beta"] = preferences.getString("mot_b");
    }
    if (preferences.isKey("flip_vert")) {
      synthesisDoc["additional_config"]["flip_vert"] = preferences.getBool("flip_vert", false);
    }
    if (preferences.isKey("flip_hori")) {
      synthesisDoc["additional_config"]["flip_hori"] = preferences.getBool("flip_hori", false);
    }
    Serial.println("Device Config:");
    serializeJson(synthesisDoc, Serial);
    Serial.println();
    preferences.end();
    synthesisDoc.clear();
  } else if (input.indexOf("state") != -1) {
    preferences.begin("config");
    synthesisDoc["wifi_state"] = WiFi.isConnected() ? "Connected" : "Disconnected";
    synthesisDoc["query_state"] = queryStateToString((QueryState) preferences.getInt("qSt", queryState));
    if (preferences.isKey("notiOptns") && preferences.getString("notiOptns", "None") != "None") {
      synthesisDoc["notification_state"] = notificationStateToString(notificationState);
    }
    if (preferences.isKey("sl_uuid")) {
      synthesisDoc["stacklight_state"] = stacklightStateToString(stacklightState);
    }
    synthesisDoc["query"] = queryResults;
    preferences.end();
    Serial.println("Device State:");
    serializeJson(synthesisDoc, Serial);
    Serial.println();
    synthesisDoc.clear();
  }
}

bool is_motion_detected(camera_fb_t* frame, int alpha, int beta) {
  bool worked = jpg2rgb565(frame->buf, frame->len, frame_565, JPG_SCALE_8X);
  if (!worked) {
    return false;
  }
  // try to detect motion
  int num_diffs = 0;
  for (int i = 0; i < FRAME_ARR_LEN; i += 2) {
    uint16_t color = (frame_565[i] << 8) | frame_565[i + 1];
    uint16_t color_old = (frame_565_old[i] << 8) | frame_565_old[i + 1];
    uint8_t b_diff = abs((color & 0b11111) - (color_old & 0b11111));
    uint8_t g_diff = abs(((color >> 5) & 0b111111) - ((color_old >> 5) & 0b111111));
    uint8_t r_diff = abs(((color >> 11) & 0b11111) - ((color_old >> 11) & 0b11111));
    if (b_diff > beta || (g_diff >> 1) > beta || r_diff > beta) {
      num_diffs++;
    }
  }
  bool motion_detected = num_diffs > alpha;

  if (motion_detected) {
    for (int i = 0; i < FRAME_ARR_LEN; i += 2) {
      frame_565_old[i] = frame_565[i];
      frame_565_old[i + 1] = frame_565[i + 1];
    }
  }

  return motion_detected;
}
