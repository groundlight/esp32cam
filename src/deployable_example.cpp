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

#ifdef PRELOADED_CREDENTIALS
  #include "credentials.h"
#endif

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

Preferences preferences;

const bool SHOW_LOGS = true;

void debug_printf(const char *format, ...) {
  if (SHOW_LOGS) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
}

void debug_println(String message) {
  if (SHOW_LOGS) {
    Serial.println(message);
  }
}
void debug_println(float message) {
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

bool disable_deep_sleep_for_notifications = false;
bool disable_deep_sleep_until_reset = true;
float targetConfidence = 0.9;
int retryLimit = 10;

String queryResults = "NONE_YET";
String queryID = "NONE_YET";
char last_label[30] = "NONE_YET";
bool wifi_configured = false;

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
    bool yellow_was_on = false;
    int delay_ms = 200;
    int flash_delay_ms = 1000;
    int inter_per_flash = flash_delay_ms / delay_ms;
    while (true) {
      for (int i = 0; i < inter_per_flash; i++) {
        String label = last_label;
        label.toUpperCase();
        if (label == "PASS" || label == "YES") {
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
          if (yellow_was_on) {
            yellow_was_on = false;
            pixels.setPixelColor(0, pixels.Color(0, 0, 0));
            pixels.setPixelColor(1, pixels.Color(0, 0, 0));
            pixels.setPixelColor(2, pixels.Color(0, 0, 0));
          } else {
            yellow_was_on = true;
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
int consecutive_pass_limit = 3;
RTC_DATA_ATTR int consecutive_pass = 0;
RTC_DATA_ATTR bool notification_sent = false;
bool shouldDoNotification(String queryRes);
bool sendNotifications(char *label, camera_fb_t *fb);
bool notifyStacklight(const char * label);
bool decodeWorkingHoursString(String working_hours);


bool should_deep_sleep() {
  return (query_delay > 29) && !disable_deep_sleep_for_notifications && !disable_deep_sleep_until_reset;
} 

void deep_sleep() {
  int time_elapsed = millis() - last_upload_time;
  int time_to_sleep = (query_delay - 10) * 1000000 - (1000 * time_elapsed);
  debug_printf("Entering deep sleep for %d seconds\n", time_to_sleep / 1000000);
  esp_sleep_enable_timer_wakeup(time_to_sleep);
  esp_deep_sleep_start();
}

#ifdef ENABLE_AP
  #include <AsyncTCP.h>
  #include <ESPAsyncWebServer.h>

  AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    form {
      display: flex;
      flex-direction: column;
      place-items: center normal;
    }
    input {
      width: 80vw;
      margin: 10px;
    }
    input[type=submit] {
      width: 100px;
      margin: 10px;
      background-color: #4CAF50;
      color: white;
      padding: 14px 20px;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    input[type=submit]:hover {
      background-color: #45a049;
    }
  </style>
  </head><body>
  <form action="/config">
    WiFi SSID: <input type="text" name="ssid" value="%ssid%">
    WiFi Password: <input type="password" name="pw" value="%password%"> 
    API Key: <input type="password" name="api_key" value="%api_key%">
    Autoconfig: <input type="checkbox" id="autoconfig" name="autoconfig" value="true" onchange="toggleAutoConfig()">
    <div id="autoConfigMessage" style="display:none;" >
      <p>If autoconfig is checked, all of the settings below will be ignored and get updated automatically.</p>
    </div>
    Detector Id: <input type="text" name="det_id" value="%det_id%">
    Query Delay (seconds): <input type="text" name="query_delay" value="%query_delay%">
    Endpoint: <input type="text" name="endpoint" value="%endpoint%">
    Target Confidence: <input type="text" name="tConf" value="%tConf%">
    Enable Motion Detector: <input type="checkbox" id="motionDetectorCheckbox" name="motionDetector" value="true" onchange="toggleMotionSettings()">
    <div id="motionSettings" style="display:none;">
      Motion Alpha (float between 0 and 1): <input type="text" name="mot_a" value="%mot_a%">
      Motion Beta (float between 0 and 1): <input type="text" name="mot_b" value="%mot_b%">
    </div>
    Enable Stacklight: <input type="checkbox" id="stacklightCheckbox" name="stacklightbox" value="true" onchange="toggleStacklightSettings()">
    <div id="stacklightSettings" style="display:none;">
      Stacklight UUID: <input type="text" name="sl_uuid" value="%sl_uuid%">
    </div>
    <input type="submit" value="Submit">
  </form>
  <script>
  function toggleAutoConfig() {
    var isChecked = document.getElementById('autoconfig').checked;
    document.getElementById('autoConfigMessage').style.display = isChecked ? 'block' : 'none';
  }
  function toggleMotionSettings() {
    var isChecked = document.getElementById('motionDetectorCheckbox').checked;
    document.getElementById('motionSettings').style.display = isChecked ? 'block' : 'none';
  }
  function toggleStacklightSettings() {
    var isChecked = document.getElementById('stacklightCheckbox').checked;
    document.getElementById('stacklightSettings').style.display = isChecked ? 'block' : 'none';
  }
  </script>
</body></html>
)rawliteral";

const char sent_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    h1 {
      font-size: 2em;
    }
  </style>
  </head><body>
  <h1>Configuration Sent!</h1>
  <a href="/">Return to Home Page</a>
</body></html>
)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

String processor(const String& var) {
  preferences.begin("config");
  // String out = String();
  String out = "";
  // if (var == "ssid") return String(ssid);
  if (var == "ssid") out = ssid;
  else if (var == "password") out = password;
  else if (var == "det_id") out = groundlight_det_id;
  else if (var == "api_key") out = groundlight_API_key;
  else if (var == "query_delay") out = String(query_delay);
  else if (var == "endpoint") out = groundlight_endpoint;
  else if (var == "tConf") out = String(targetConfidence);
  else if (var == "mot_a" && preferences.isKey("mot_a")) out = String(preferences.getString("mot_a", "0.0"));
  else if (var == "mot_b" && preferences.isKey("mot_b")) out = String(preferences.getString("mot_b", "0.0"));
  else if (var == "sl_uuid" && preferences.isKey("sl_uuid")) out = preferences.getString("sl_uuid", "");
  else if (var == "autoconfig" && preferences.isKey("autoconfig")) out = preferences.getString("autoconfig", "");
  preferences.end();
  return out;
  // return var;
}

bool shouldPerformAutoConfig(AsyncWebServerRequest *request) {
    bool autoconfigEnabled = request->hasParam("autoconfig");
    bool ssidFilled = request->hasParam("ssid") && request->getParam("ssid")->value() != "";
    bool passwordFilled = request->hasParam("pw") && request->getParam("pw")->value() != "";
    bool apiKeyFilled = request->hasParam("api_key") && request->getParam("api_key")->value() != "";

    return autoconfigEnabled && ssidFilled && passwordFilled && apiKeyFilled;
}

void performAutoConfig(AsyncWebServerRequest *request){
  const char* endpoint = groundlight_endpoint;
  const char* apiToken = groundlight_API_key;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac = mac.substring(6);
  String esp_detector_name = (StringSumHelper) "ESP32-CAM-" + mac;
  debug_printf("esp cam detector name:  %s\n",esp_detector_name.c_str());
  detector esp_det = get_detector_by_name(endpoint, esp_detector_name.c_str(), apiToken); 
  if (strcmp(esp_det.id, "NONE") == 0) {
    preferences.putString("det_id", "DETECTOR NOT FOUND");
    Serial.println("Error: Detector not found.");
    return; 
  }
  preferences.putString("det_id", esp_det.id);
  strcpy(groundlight_det_id, esp_det.id);
  String metadataStr = esp_det.metadata; 
  DynamicJsonDocument metadataDoc(1024);
  DeserializationError error = deserializeJson(metadataDoc, metadataStr);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return; 
  }
  if (metadataDoc.containsKey("Query Delay (seconds)") && !metadataDoc["Query Delay (seconds)"].isNull()) {
    query_delay = metadataDoc["Query Delay (seconds)"];
    preferences.putInt("query_delay", query_delay);
  }
  if (metadataDoc.containsKey("Target Confidence") && !metadataDoc["Target Confidence"].isNull()) {
    targetConfidence = metadataDoc["Target Confidence"];
    preferences.putFloat("tConf", targetConfidence);
  }
  if (metadataDoc.containsKey("Motion Alpha (float between 0 and 1)") && !metadataDoc["Motion Alpha (float between 0 and 1)"].isNull()){
    String mot_a = metadataDoc["Motion Alpha (float between 0 and 1)"];
    preferences.putString("mot_a",mot_a);
  } else if (metadataDoc.containsKey("Motion Alpha (float between 0 and 1)") && metadataDoc["Motion Alpha (float between 0 and 1)"].isNull()){
    preferences.remove("mot_a");
  }
  if (metadataDoc.containsKey("Motion Beta (float between 0 and 1)") && !metadataDoc["Motion Beta (float between 0 and 1)"].isNull()){
    String mot_b = metadataDoc["Motion Beta (float between 0 and 1)"];
    preferences.putString("mot_b",mot_b);
  } else if(metadataDoc.containsKey("Motion Beta (float between 0 and 1)") && metadataDoc["Motion Beta (float between 0 and 1)"].isNull()){
    preferences.remove("mot_b");
  }
  if (metadataDoc.containsKey("Stacklight UUID") && !metadataDoc["Stacklight UUID"].isNull()){
    String sl_uuid = metadataDoc["Stacklight UUID"];
    preferences.putString("sl_uuid",sl_uuid);
  } else if (metadataDoc.containsKey("Stacklight UUID") && metadataDoc["Stacklight UUID"].isNull()){
    preferences.remove("sl_uuid");
  }
} 
#endif

void setup() {

  Serial.begin(115200);
  Serial.println("Groundlight ESP32CAM waking up...");
 
 
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wakeup from deep sleep.  forcing restart to properly reset wifi module");
    ESP.restart();
  }

#if defined(GPIO_LED_FLASH)
  pinMode(GPIO_LED_FLASH, OUTPUT);
  digitalWrite(GPIO_LED_FLASH, LOW);
#endif
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
#endif

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  debug_printf("ESP32 Chip Revision %d\n", chip_info.revision);
  debug_printf("WiFi MAC Address: %s\n", WiFi.macAddress().c_str());
  
  debug_printf("Firmware : %s built on %s at %s\n", NAME, __DATE__, __TIME__);

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
      debug_println("Reset settings!");
    }
  }

  #ifdef PRELOADED_CREDENTIALS
    set_preferences(preferences);
  #endif


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
  if (preferences.isKey("ap_pw")) {
    WiFi.softAP((StringSumHelper) "ESP32-CAM-" + mac, preferences.getString("ap_pw", "").c_str());
  } else {
    WiFi.softAP((StringSumHelper) "ESP32-CAM-" + mac);
  }
  preferences.end();
  // Send web page with input fields to client
  // at http://192.168.4.1/
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  }); 

  server.on("/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    preferences.begin("config", false);
    if (request->hasParam("ssid") && request->getParam("ssid")->value() != "") {
      preferences.putString("ssid", request->getParam("ssid")->value());
      strcpy(ssid, request->getParam("ssid")->value().c_str());
    }
    if (request->hasParam("pw") && request->getParam("pw")->value() != "") {
      preferences.putString("password", request->getParam("pw")->value());
      strcpy(password, request->getParam("pw")->value().c_str());
    }
    if (request->hasParam("api_key") && request->getParam("api_key")->value() != "") {
      preferences.putString("api_key", request->getParam("api_key")->value());
      strcpy(groundlight_API_key, request->getParam("api_key")->value().c_str());
    }
    
    if (shouldPerformAutoConfig(request)){
      performAutoConfig(request);
    } else {
      if (request->hasParam("det_id") && request->getParam("det_id")->value() != "") {
        preferences.putString("det_id", request->getParam("det_id")->value());
        strcpy(groundlight_det_id, request->getParam("det_id")->value().c_str());
      }
      if (request->hasParam("query_delay") && request->getParam("query_delay")->value() != "") {
        query_delay = request->getParam("query_delay")->value().toInt();
        preferences.putInt("query_delay", query_delay);
      }
      if (request->hasParam("endpoint") && request->getParam("endpoint")->value() != "") {
        preferences.putString("endpoint", request->getParam("endpoint")->value());
        strcpy(groundlight_endpoint, request->getParam("endpoint")->value().c_str());
      }
      if (request->hasParam("tConf") && request->getParam("tConf")->value() != "") {
        targetConfidence = request->getParam("tConf")->value().toFloat();
        preferences.putFloat("tConf", targetConfidence);
      }
      if (request->hasParam("mot_a") && request->getParam("mot_a")->value() != "") {
        preferences.putString("mot_a", request->getParam("mot_a")->value());
      }
      if (request->hasParam("mot_b") && request->getParam("mot_b")->value() != "") {
        preferences.putString("mot_b", request->getParam("mot_b")->value());
      }
      if (request->hasParam("sl_uuid") && request->getParam("sl_uuid")->value() != "") {
        preferences.putString("sl_uuid", request->getParam("sl_uuid")->value());
      }
      if (request->hasParam("autoconfig") && request->getParam("autoconfig")->value() != "") {
        preferences.putString("autoconfig", request->getParam("autoconfig")->value());
      }

    }
    // request->send(200, "text/html", "Configuration sent to your ESP Camera<br><a href=\"/\">Return to Home Page</a>");
    request->send_P(200, "text/html", sent_html);
    preferences.end();
  });
  server.onNotFound(notFound);
  server.begin();
#endif
  preferences.begin("config", false);
  if (preferences.isKey("ssid") && preferences.isKey("password") && preferences.isKey("api_key") && preferences.isKey("det_id") && preferences.isKey("query_delay")) {
    preferences.getString("ssid", ssid, 100);
    preferences.getString("password", password, 100);
    preferences.getString("api_key", groundlight_API_key, 75);
    preferences.getString("det_id", groundlight_det_id, 100);
    query_delay = preferences.getInt("query_delay", query_delay);
    
    WiFi.begin(ssid, password);
    wifi_configured = true;
  }
#ifdef ENABLE_STACKLIGHT
  if (preferences.isKey("ssid") && preferences.isKey("sl_uuid") && !preferences.isKey("sl_ip")) {

    WiFi.disconnect();
    debug_println("Initializing Stacklight through AP");
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
          debug_printf("Stacklight %s initialized at sl ip %s\n", preferences.getString("sl_uuid", "").c_str(), res.c_str());
        }
      } else {
        debug_printf("Could not connect to stacklight : %s\n", SSID.c_str());
      }
      WiFi.disconnect();
    } else {
      debug_printf("Could not find stacklight : %s\n", preferences.getString("sl_uuid", "").c_str());
    }
    WiFi.begin(ssid, password);
  }
#endif

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
    debug_printf("Camera initialization failed with error code: %s\n", esp_err_to_name(error_code));
    debug_printf("Restarting system in 3 seconds!\n");

    delay(3000);
    ESP.restart(); // some boards are less reliable for initialization and will everntually just start working
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  preferences.begin("config", false);
  if (!preferences.getBool("img_rotate", false)) s->set_vflip(s, 1);
  if (preferences.getBool("img_mirror", false)) s->set_hmirror(s, 1);
  preferences.end();

  // alloc memory for 565 frames
  frame_565 = (uint8_t *) ps_malloc(FRAME_ARR_LEN);
  frame_565_old = (uint8_t *) ps_malloc(FRAME_ARR_LEN);
  
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
      debug_println("New data");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      if (((String) input3).indexOf("query") != -1 && ((String) input3).indexOf("ssid") == -1) {
        try_answer_query(input3);
      } else if(try_save_config(input3)) {
        debug_println("Saved config!");
        WiFi.begin(ssid, password);
        wifi_configured = true;
      }
      input = "";
      input3_index = 0;
      new_data_ = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void loop () {

  if (!wifi_configured) {
    if (millis() > last_print_time + 1000) {
      debug_println("WiFi not started (no configuration found!)");
      last_print_time = millis();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
  } else if (millis() > last_print_time + 1000) {
    last_print_time = millis();
  }

    // normalize startup time to 10 seconds if we could be coming from sleep (delay is built into sleep delay)
  if (should_deep_sleep()) {
    int time = millis();
    if (time < 10000) {
      vTaskDelay((10000 - time) / portTICK_PERIOD_MS);
    } else {
      debug_printf("Startup (or wake-up from deep sleep) took more than 10s!\n");
    }
  }

  if (millis() < last_upload_time + query_delay * 1000 && !should_deep_sleep()) {
    return;
  } else {
    last_upload_time = millis();
  }

  debug_printf("Free heap size: %d\n", esp_get_free_heap_size());

  preferences.begin("config", true);
  if (preferences.isKey("wkhrs") && preferences.getString("wkhrs", "") != "") {
    debug_printf("Checking time of day vs. working hours configuration\n");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)){
      debug_println("Failed to obtain time");
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
        debug_println("Not in working hours!");
        last_upload_time = millis();
        preferences.end();
        if (should_deep_sleep()) {
          deep_sleep();
        }
        return;
      }
    }
  }
  preferences.end();

  debug_printf("Capturing image...");

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
    debug_printf("Camera capture failed! Restarting system in 3 seconds!\n");
    delay(3000);
    ESP.restart(); // some boards are less reliable for camera captures and will everntually just start working
  }

  debug_printf("encoded size is %d bytes\n", frame->len);

  preferences.begin("config");
  if (preferences.isKey("motion") && preferences.getBool("motion") && preferences.isKey("mot_a") && preferences.isKey("mot_b")) {
    int alpha = round(preferences.getString("mot_a", "0.0").toFloat() * (float) FRAME_ARR_LEN);
    int beta = round(preferences.getString("mot_b", "0.0").toFloat() * (float) COLOR_VAL_MAX);
    if (is_motion_detected(frame, alpha, beta)) {
      debug_println("Motion detected!");
    } else {
      esp_camera_fb_return(frame);
      if (should_deep_sleep()) {
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
    debug_printf("having difficulty connection to WIFI SSID %s... status code : %d\n", ssid, WiFi.status());
    for (int i = 0; i < 100 && !WiFi.isConnected(); i++) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
    if (WiFi.isConnected()) {
      debug_printf("WIFI connected to SSID %s\n", ssid);
      

  } else {
      debug_printf("unable to connect to wifi status code %d! (skipping image query and looping again)\n", WiFi.status());
      return;
  }

  debug_printf("Submitting image query to Groundlight...");

  queryResults = submit_image_query(frame, groundlight_endpoint, groundlight_det_id, groundlight_API_key);
  queryID = get_query_id(queryResults);

  debug_printf("Query ID: %s\n", queryID.c_str());

  if (queryID == "NONE" || queryID == "") {
    debug_println("Failed to get query ID");
    esp_camera_fb_return(frame);
    return;
  }

  debug_printf("Current confidence: %f / Target confidence %f\n", get_query_confidence(queryResults), targetConfidence);

  // wait for confident answer
  int currTime = millis();
  while (get_query_confidence(queryResults) < targetConfidence) {
    debug_println("Waiting for confident answer...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    queryResults = get_image_query(groundlight_endpoint, queryID.c_str(), groundlight_API_key);

    if (millis() > currTime + retryLimit * 1000) {
      debug_println("Retry limit reached!");
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
            queryState = QueryState::LAST_RESPONSE_PASS;
          } else if (label == "FAIL" || label == "NO") {
            queryState = QueryState::LAST_RESPONSE_FAIL;
          } else if (label == "UNSURE" || label == "__UNSURE") {
            queryState = QueryState::LAST_RESPONSE_UNSURE;
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
    #ifdef ENABLE_STACKLIGHT
      preferences.begin("config");
      if (preferences.isKey("sl_uuid")) {
        if (!notifyStacklight(last_label)) {
          debug_println("Failed to notify stacklight");
        }
      }
    #endif
    resultDoc.clear();
    preferences.end();
    if (WiFi.SSID() != ssid) {
      WiFi.disconnect();
      vTaskDelay(500 / portTICK_PERIOD_MS);
      WiFi.begin(ssid, password);
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
  } else {
    debug_println("Failed to parse query results");
    debug_println(error.c_str());
  }

  esp_camera_fb_return(frame);

  if (should_deep_sleep()) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    deep_sleep();
  }

  debug_printf("waiting %d seconds between queries...\n", query_delay);
}

StaticJsonDocument<4096> doc;

bool try_save_config(char * input) {

  ArduinoJson::DeserializationError err = deserializeJson(doc, input);

  if (err.code() != ArduinoJson::DeserializationError::Ok) {
    debug_println("Failed to parse input as JSON");
    debug_println(err.c_str());
    return false;
  }

  if (!doc.containsKey("ssid") || !doc.containsKey("password") || !doc.containsKey("api_key") || !doc.containsKey("det_id") || !doc.containsKey("cycle_time")) {
    debug_println("Missing required fields");
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
    debug_println("Found additional config!");
    if (doc["additional_config"].containsKey("target_confidence")) {
      debug_println("Found target confidence!");
      String targetConfidenceString = (const char *)doc["additional_config"]["target_confidence"];
      debug_println(targetConfidenceString);
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
      debug_println("Found notification options!");
      preferences.putString("notiOptns", (const char *)doc["additional_config"]["notificationOptions"]);
      if (doc["additional_config"].containsKey("slack") && doc["additional_config"]["slack"].containsKey("slackKey")) {
        debug_println("Found slack!");
        preferences.putString("slackKey", (const char *)doc["additional_config"]["slack"]["slackKey"]);
        preferences.putString("slackEndpoint", (const char *)doc["additional_config"]["slack"]["slackEndpoint"]);
      } else {
        preferences.remove("slackKey");
        preferences.remove("slackEndpoint");
      }
      if (doc["additional_config"].containsKey("twilio") && doc["additional_config"]["twilio"].containsKey("twilioKey")) {
        debug_println("Found twilio!");
        preferences.putString("twilioSID", (const char *)doc["additional_config"]["twilio"]["twilioSID"]);
        preferences.putString("twilioKey", (const char *)doc["additional_config"]["twilio"]["twilioKey"]);
        preferences.putString("twilioNumber", (const char *)doc["additional_config"]["twilio"]["twilioNumber"]);
        preferences.putString("twilioEndpoint", (const char *)doc["additional_config"]["twilio"]["twilioEndpoint"]);
      } else {
        preferences.remove("twilioKey");
      }
      if (doc["additional_config"].containsKey("email") && doc["additional_config"]["email"].containsKey("emailKey")) {
        debug_println("Found email!");
        preferences.putString("emailKey", (const char *)doc["additional_config"]["email"]["emailKey"]);
        preferences.putString("emailEndpoint", (const char *)doc["additional_config"]["email"]["emailEndpoint"]);
        preferences.putString("email", (const char *)doc["additional_config"]["email"]["email"]);
        preferences.putString("emailHost", (const char *)doc["additional_config"]["email"]["emailHost"]);
      } else {
        preferences.remove("emailKey");
      }
    }
    if (doc["additional_config"].containsKey("stacklight") && doc["additional_config"]["stacklight"].containsKey("uuid")) {
      debug_println("Found stacklight!");
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
      debug_println("Has working hours!");
      preferences.putString("wkhrs", (const char *)doc["additional_config"]["working_hours"]);
    } else {
      preferences.remove("wkhrs");
    }
    if (doc["additional_config"].containsKey("motion_detection")) {
      debug_println("Has motion detection!");
      preferences.putBool("motion", true);
      if (doc["additional_config"]["motion_detection"].containsKey("alpha")) {
        debug_println("Has alpha!");
        preferences.putString("mot_a",(const char *) doc["additional_config"]["motion_detection"]["alpha"]);
        preferences.putString("mot_b",(const char *) doc["additional_config"]["motion_detection"]["beta"]);
      } else {
        preferences.remove("mot_a");
        preferences.remove("mot_b");
      }
    } else {
      preferences.remove("motion");
    }
    if (doc["additional_config"].containsKey("img_rotate")) {
      debug_println("Image rotation found in configuration!");
      preferences.putBool("img_rotate", doc["additional_config"]["img_rotate"]);
      sensor_t * s = esp_camera_sensor_get();
      s->set_vflip(s, 0);
    } else {
      preferences.remove("img_rotate");
      sensor_t * s = esp_camera_sensor_get();
      s->set_vflip(s, 1);
    }
    if (doc["additional_config"].containsKey("img_mirror")) {
      debug_println("Image mirroring found in configuration!");
      preferences.putBool("img_mirror", doc["additional_config"]["img_mirror"]);
      sensor_t * s = esp_camera_sensor_get();
      s->set_hmirror(s, 1);
    } else {
      preferences.remove("img_mirror");
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
    } else if (notiOptns == "On Change") { // TODO: last_label isn't stored in persistant memory
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
    } else if (notiOptns == "On 2 Yes") {
        if (resultDoc["result"]["label"] != "QUERY_FAIL") {
          String label = resultDoc["result"]["label"];
          label.toUpperCase();
          debug_println("What label did we get?");
          debug_println(label);
          bool sanity = (label == "PASS");
          if (label == "PASS" || label == "YES") {
            debug_println("incrementing consecutive_pass");
            consecutive_pass++;
            disable_deep_sleep_for_notifications = true;
          }
          else {
            consecutive_pass = 0;
            notification_sent = false;
            disable_deep_sleep_for_notifications = false;
          }
        if (consecutive_pass >= consecutive_pass_limit) {
          if (notification_sent == false) {
            debug_println("Consecutive pass criteria met.  Send notification!");
            res = true;
            notification_sent = true;
            disable_deep_sleep_for_notifications = false;
        }
      }
    }
  }
    strcpy(last_label, resultDoc["result"]["label"]);
    preferences.end();
    return res;
  }
  else {
    return false;
  }
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
    debug_println("Sending Slack notification...");
    String slackKey = preferences.getString("slackKey", "");
    String slackEndpoint = preferences.getString("slackEndpoint", "");
    worked = worked && sendSlackNotification(det_name, det_query, slackKey, slackEndpoint, label, fb) == SlackNotificationResult::SUCCESS;
  }
  if (preferences.isKey("twilioKey") && preferences.isKey("twilioNumber") && preferences.isKey("twilioEndpoint")) {
    debug_println("Sending Twilio notification...");
    String twilioSID = preferences.getString("twilioSID", "");
    String twilioKey = preferences.getString("twilioKey", "");
    String twilioNumber = preferences.getString("twilioNumber", "");
    String twilioEndpoint = preferences.getString("twilioEndpoint", "");
    worked = worked && sendTwilioNotification(det_name, det_query, twilioSID, twilioKey, twilioNumber, twilioEndpoint, label, fb) == TwilioNotificationResult::SUCCESS;
  }
  if (preferences.isKey("emailKey") && preferences.isKey("email") && preferences.isKey("emailEndpoint")) {
    debug_println("Sending Email notification...");
    String emailKey = preferences.getString("emailKey", "");
    String email = preferences.getString("email", "");
    String emailEndpoint = preferences.getString("emailEndpoint", "");
    String host = preferences.getString("emailHost", "");
    worked = worked && sendEmailNotification(det_name, det_query, emailKey, email, emailEndpoint, host, label, fb) == EmailNotificationResult::SUCCESS;
  }
  preferences.end();
  return worked;
}
#ifdef ENABLE_STACKLIGHT
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

  debug_println("Connecting to Stacklight AP");
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
      debug_println("Couldn't connect to stacklight");
      return false;
    }
  } else {
    debug_println("Couldn't find stacklight");
    stacklightState = STACKLIGHT_NOT_FOUND;
    return false;
  }

  bool isKey = preferences.isKey("sl_ip");
  preferences.end();
  return isKey;
}
#endif

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

   // this is a blunt hammer but maybe necessary
   // should only get called if we are communicating via serial with the device
  disable_deep_sleep_until_reset = true;
  Serial.println("WARNING! Sleep is disabled until ESP.restart()");

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
    if (preferences.isKey("img_rotate")) {
      synthesisDoc["additional_config"]["img_rotate"] = preferences.getBool("img_rotate", false);
    }
    if (preferences.isKey("img_mirror")) {
      synthesisDoc["additional_config"]["img_mirror"] = preferences.getBool("img_mirror", false);
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