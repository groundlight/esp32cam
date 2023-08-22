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

#ifdef CAMERA_MODEL_M5STACK_PSRAM
  #define RESET_SETTINGS_GPIO         38
  #define RESET_SETTINGS_GPIO_DEFAULT LOW
#else
  #define RESET_SETTINGS_GPIO         -1
  #define RESET_SETTINGS_GPIO_DEFAULT HIGH
#endif

camera_fb_t *frame = NULL;
int *last_frame_buffer = NULL;
char groundlight_endpoint[60] = "api.groundlight.ai";

Preferences preferences;

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

// bool try_save_config(String input);
bool try_save_config(char * input);
void printInfo();
bool shouldDoNotification(String queryRes);
void sendNotifications(char *label, camera_fb_t *fb);
bool notifyStacklight(const char * label);
bool decodeWorkingHoursString(String working_hours);
void deep_sleep() {
  int time_elapsed = millis() - last_upload_time;
  int time_to_sleep = (query_delay - 10) * 1000000 - (1000 * time_elapsed);
  // Serial.println((StringSumHelper)"Going to sleep for " + String(time_to_sleep) + " microseconds");
  esp_sleep_enable_timer_wakeup(time_to_sleep);
  // esp_sleep_enable_uart_wakeup(0);
  // esp_sleep_config_gpio_isolate();
  esp_deep_sleep_start();
}

void setup()
{

#if defined(GPIO_LED_FLASH)
  pinMode(GPIO_LED_FLASH, OUTPUT);
  digitalWrite(GPIO_LED_FLASH, LOW);
#endif

  // Serial.begin(115200);
  Serial.begin(9600);
  Serial.println("Edgelight waking up...");
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
      Serial.println("Reset settings!");
    }
  }

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

  Serial.println("Configuring Camera...");

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
  config.frame_size = FRAMESIZE_UXGA; // See here for a list of options and resolutions: https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h#L84
  config.jpeg_quality = 10;           // lower means higher quality
  config.fb_count = 1;

  // if (config.frame_size == FRAMESIZE_UXGA) {
  //   last_frame_buffer = (int *)malloc(1600*1200*3);
  // } else {
  //   Serial.println("Camera frame size is not UXGA. Motion detection will not work.");
  // }

  // Testing how to get the latest image
  // config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t error_code = esp_camera_init(&config);
  if (error_code != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", error_code);
    Serial.println("Restarting system!");
    delay(3000);
    ESP.restart(); // some boards are less reliable for initialization and will everntually just start working
    return;
  }

  Serial.printf("using detector : %s\n", groundlight_det_id);

  delay(2000);
}

void loop()
{
  while (Serial.available() > 0 && new_data == false) {
    // input += Serial.readString();
    input2[input2_index] = Serial.read();
    if (input2[input2_index] == '\n') {
      input2[input2_index] = '\0';
      new_data = true;
    }
    input2_index++;
  }
  
  if (new_data) {
    if(try_save_config(input2)) {
      Serial.println("Saved config!");
      WiFi.begin(ssid, password);
      wifi_connected = true;
    }
    input = "";
    input2_index = 0;
    new_data = false;
  }

  if (!wifi_connected && millis() > last_print_time + 1000) {
    Serial.println("Waiting for Credentials...");
    last_print_time = millis();
    return;
  } else if (millis() > last_print_time + 1000) {
    last_print_time = millis();
    // printInfo();
  }

  if (should_deep_sleep) {
    int time = millis();
    // make sure we don't startup in less than 10 seconds
    if (time < 10000) {
      delay(10000 - time);
    } else {
      Serial.println("Took too long to startup!");
    }
  }

  if (millis() < last_upload_time + query_delay * 1000 && !should_deep_sleep) {
    // delay(500);
    return;
  } else {
    last_upload_time = millis();
    Serial.println("Taking a lap!");
  }

  preferences.begin("config", true);
  if (preferences.isKey("wkhrs") && preferences.getString("wkhrs", "") != "") {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
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
        Serial.println("Not in working hours!");
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
    delay(1000);
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
    Serial.println("Camera capture failed! Restarting system!");
    delay(3000);
    ESP.restart(); // maybe this will fix things? hard to say. its not going to be worse
  }

  Serial.printf("Captured image. Encoded size is %d bytes\n", frame->len);

  preferences.begin("config");
  if (preferences.isKey("motion") && preferences.getBool("motion")) {
    Serial.println("Checking for motion...");
    Serial.println("Buffer size" + frame->len);
  }
  preferences.end();

  // wait for wifi connection
  if (!WiFi.isConnected()) {
    Serial.println("Waiting for wifi connection...");
    for (int i = 0; i < 100 && !WiFi.isConnected(); i++) {
      delay(100);
    }
  }

  queryResults = submit_image_query(frame, groundlight_endpoint, groundlight_det_id, groundlight_API_key);
  String queryId = get_query_id(queryResults);

  Serial.println("Query ID:");
  Serial.println(queryId);
  Serial.println("Confidence:");
  Serial.println(get_query_confidence(queryResults));
  Serial.println();

  // wait for confident answer
  int currTime = millis();
  while (get_query_confidence(queryResults) < targetConfidence)
  {
    Serial.println("Waiting for confident answer...");
    delay(1000);
    queryResults = get_image_query(groundlight_endpoint, queryId.c_str(), groundlight_API_key);

    if (millis() > currTime + retryLimit * 1000) {
      Serial.println("Retry limit reached!");
      break;
    }
  }

  Serial.println();
  Serial.println("Query Results:");
  Serial.println(queryResults);
  Serial.println();
  deserializeJson(resultDoc, queryResults);
  if (shouldDoNotification(queryResults)) {
    Serial.println("Sending notification...");
    sendNotifications(last_label, frame);
  }
  resultDoc.clear();
  preferences.begin("config");
  if (preferences.isKey("sl_uuid")) {
    if (!notifyStacklight(last_label)) {
      Serial.println("Failed to notify stacklight");
    }
  }
  preferences.end();
  if (WiFi.SSID() != ssid) {
    WiFi.disconnect();
    delay(500);
    WiFi.begin(ssid, password);
    delay(500);
  }

  esp_camera_fb_return(frame);

  if (should_deep_sleep) {
    delay(500);
    deep_sleep();
  }

  Serial.printf("waiting %ds between queries...", query_delay);
}

StaticJsonDocument<4096> doc;

bool try_save_config(char * input) {

  ArduinoJson::DeserializationError err = deserializeJson(doc, input);

  if (err.code() != ArduinoJson::DeserializationError::Ok) {
    Serial.println("Failed to parse input as JSON");
    Serial.println(err.c_str());
    return false;
  }

  if (!doc.containsKey("ssid") || !doc.containsKey("password") || !doc.containsKey("api_key") || !doc.containsKey("det_id") || !doc.containsKey("cycle_time")) {
    Serial.println("Missing required fields");
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
  if (doc.containsKey("tConf")) {
    preferences.putFloat("tConf", doc["targetConfidence"]);
    targetConfidence = doc["targetConfidence"];
  } else {
    preferences.remove("tConf");
  }
  if (doc.containsKey("waitTime")) {
    preferences.putInt("waitTime", doc["waitTime"]);
    retryLimit = doc["waitTime"];
  } else {
    preferences.remove("waitTime");
  }
  detector det = get_detector_by_id(groundlight_endpoint, (const char *)doc["det_id"], (const char *)doc["api_key"]);
  delay(1000);
  Serial.println("doc Info:");
  serializeJson(doc, Serial);
  delay(1000);
  preferences.putString("det_name", det.name);
  preferences.putString("det_query", det.query);
  if (doc.containsKey("additional_config")) {
    Serial.println("Found additional config!");
    delay(100);
    if (doc["additional_config"].containsKey("endpoint") && doc["additional_config"]["endpoint"] != "") {
      preferences.putString("endpoint", (const char *)doc["additional_config"]["endpoint"]);
      strcpy(groundlight_endpoint, (const char *)doc["additional_config"]["endpoint"]);
    } else {
      preferences.remove("endpoint");
    }
    if (doc["additional_config"].containsKey("notificationOptions") && doc["additional_config"]["notificationOptions"] != "None") {
      Serial.println("Found notification options!");
      delay(100);
      preferences.putString("notiOptns", (const char *)doc["additional_config"]["notificationOptions"]);  
      if (doc["additional_config"].containsKey("slack") && doc["additional_config"]["slack"].containsKey("slackKey")) {
        Serial.println("Found slack!");
        delay(100);
        preferences.putString("slackKey", (const char *)doc["additional_config"]["slack"]["slackKey"]);
        preferences.putString("slackEndpoint", (const char *)doc["additional_config"]["slack"]["slackEndpoint"]);
      } else {
        preferences.remove("slackKey");
      }
      // Serial.print
      if (doc["additional_config"].containsKey("twilio") && doc["additional_config"]["twilio"].containsKey("twilioKey")) {
        Serial.println("Found twilio!");
        delay(100);
        preferences.putString("twilioSID", (const char *)doc["additional_config"]["twilio"]["twilioSID"]);
        preferences.putString("twilioKey", (const char *)doc["additional_config"]["twilio"]["twilioKey"]);
        preferences.putString("twilioNumber", (const char *)doc["additional_config"]["twilio"]["twilioNumber"]);
        preferences.putString("twilioEndpoint", (const char *)doc["additional_config"]["twilio"]["twilioEndpoint"]);
      } else {
        preferences.remove("twilioKey");
      }
      if (doc["additional_config"].containsKey("email") && doc["additional_config"]["email"].containsKey("emailKey")) {
        Serial.println("Found email!");
        delay(100);
        preferences.putString("emailKey", (const char *)doc["additional_config"]["email"]["emailKey"]);
        preferences.putString("emailEndpoint", (const char *)doc["additional_config"]["email"]["emailEndpoint"]);
        preferences.putString("email", (const char *)doc["additional_config"]["email"]["email"]);
        preferences.putString("emailHost", (const char *)doc["additional_config"]["email"]["emailHost"]);
      } else {
        preferences.remove("emailKey");
      }
    }
    if (doc["additional_config"].containsKey("stacklight") && doc["additional_config"]["stacklight"].containsKey("uuid")) {
      Serial.println("Found stacklight!");
      delay(100);
      preferences.putString("sl_uuid", (const char *)doc["additional_config"]["stacklight"]["uuid"]);
    } else {
      preferences.remove("sl_uuid");
    }
    if (doc["additional_config"].containsKey("working_hours")) {
      Serial.println("Has working hours!");
      delay(100);
      preferences.putString("wkhrs", (const char *)doc["additional_config"]["working_hours"]);
    } else {
      preferences.remove("wkhrs");
    }
    if (doc["additional_config"].containsKey("motion_detection")) {
      Serial.println("Has motion detection!");
      delay(100);
      preferences.putBool("motion", true);
    } else {
      preferences.remove("motion");
    }
  }
  preferences.end();

  strcpy(ssid, (const char *)doc["ssid"]);
  strcpy(password, (const char *)doc["password"]);
  strcpy(groundlight_API_key, (const char *)doc["api_key"]);
  strcpy(groundlight_det_id, (const char *)doc["det_id"]);
  query_delay = doc["cycle_time"];

  preferences.begin("config", true);
  decodeWorkingHoursString(preferences.getString("wkhrs", ""));
  preferences.end();

  return true;
}

void printInfo() {
  preferences.begin("config", true);
  Serial.print("NotiOptns: ");
  Serial.println(preferences.getString("notiOptns", "None"));
  Serial.print("Endpoint: ");
  Serial.println(preferences.getString("endpoint", "api.groundlight.ai"));
  Serial.print("Det Name: ");
  Serial.println(preferences.getString("det_name", "None"));
  Serial.print("Det Query: ");
  Serial.println(preferences.getString("det_query", "None"));
  Serial.print("Slack Key: ");
  Serial.println(preferences.getString("slackKey", "None"));
  Serial.print("Slack Endpoint: ");
  Serial.println(preferences.getString("slackEndpoint", "None"));
  Serial.print("Twilio SID: ");
  Serial.println(preferences.getString("twilioSID", "None"));
  Serial.print("Twilio Key: ");
  Serial.println(preferences.getString("twilioKey", "None"));
  Serial.print("Twilio Number: ");
  Serial.println(preferences.getString("twilioNumber", "None"));
  Serial.print("Twilio Endpoint: ");
  Serial.println(preferences.getString("twilioEndpoint", "None"));
  Serial.print("Email Key: ");
  Serial.println(preferences.getString("emailKey", "None"));
  Serial.print("Email Endpoint: ");
  Serial.println(preferences.getString("emailEndpoint", "None"));
  Serial.print("Email: ");
  Serial.println(preferences.getString("email", "None"));
  Serial.print("Email Host: ");
  Serial.println(preferences.getString("emailHost", "None"));
  preferences.end();
}

bool shouldDoNotification(String queryRes) {
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

String stacklight_ap_ip = "http://192.168.4.1:8080";

void sendNotifications(char *label, camera_fb_t *fb) {
  preferences.begin("config");
  String det_name = preferences.getString("det_name", "");
  String det_query = preferences.getString("det_query", "");
  if (det_name == "NONE") {
    detector det = get_detector_by_id(groundlight_endpoint, groundlight_det_id, groundlight_API_key);
    det_name = det.name;
    det_query = det.query;
    preferences.putString("det_name", det_name);
    preferences.putString("det_query", det_query);
  }
  if (preferences.isKey("slackKey") && preferences.isKey("slackEndpoint")) {
    Serial.println("Sending Slack notification...");
    String slackKey = preferences.getString("slackKey", "");
    String slackEndpoint = preferences.getString("slackEndpoint", "");
    sendSlackNotification(det_name, det_query, slackKey, slackEndpoint, label, fb);
  }
  if (preferences.isKey("twilioKey") && preferences.isKey("twilioNumber") && preferences.isKey("twilioEndpoint")) {
    Serial.println("Sending Twilio notification...");
    String twilioSID = preferences.getString("twilioSID", "");
    String twilioKey = preferences.getString("twilioKey", "");
    String twilioNumber = preferences.getString("twilioNumber", "");
    String twilioEndpoint = preferences.getString("twilioEndpoint", "");
    sendTwilioNotification(det_name, det_query, twilioSID, twilioKey, twilioNumber, twilioEndpoint, label, fb);
  }
  if (preferences.isKey("emailKey") && preferences.isKey("email") && preferences.isKey("emailEndpoint")) {
    Serial.println("Sending Email notification...");
    String emailKey = preferences.getString("emailKey", "");
    String email = preferences.getString("email", "");
    String emailEndpoint = preferences.getString("emailEndpoint", "");
    String host = preferences.getString("emailHost", "");
    sendEmailNotification(det_name, det_query, emailKey, email, emailEndpoint, host, label, fb);
  }
  preferences.end();
}

bool notifyStacklight(const char * label) {
  preferences.begin("config");
  if (!preferences.isKey("sl_uuid")) {
    preferences.end();
    return false;
  }

  if (!preferences.isKey("sl_ip")) {
    Serial.println("Connecting to Stacklight AP");
    // check if is access point
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      String SSID = WiFi.SSID(i);
      Serial.print("SSID: ");
      Serial.println(SSID);
      if (SSID == ((const StringSumHelper)"GL_STACKLIGHT_" + preferences.getString("sl_uuid", ""))) {
        // connect and move it to wifi network
        WiFi.begin(SSID, (const StringSumHelper)"gl_stacklight_password_" + preferences.getString("sl_uuid", ""));
        // delay(5000);
        for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
          delay(500);
        }
        if(WiFi.status() == WL_CONNECTED) {
          WiFiClient client;
          HTTPClient http;
          http.setReuse(true);
          http.begin(client, stacklight_ap_ip);
          http.addHeader("Content-Type", "application/json");
          int httpResponseCode = http.POST((const StringSumHelper)"{\"ssid\":\"" + ssid + "\",\"password\":\"" + password + "\"}");
          Serial.print("HTTP Response code: ");
          Serial.println(httpResponseCode);
          http.end();

          delay(5000);
          
          http.begin((StringSumHelper)stacklight_ap_ip + "/ip");
          httpResponseCode = http.GET();
          
          if (httpResponseCode>0 && httpResponseCode == HTTP_CODE_OK) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String payload = http.getString();
            if (payload != "Not connected to WiFi") {
              Serial.println(payload);
              preferences.putString("sl_ip", payload);
            }
          } else {
            Serial.print("Error code: ");
            Serial.println(httpResponseCode);
          }
          // Free resources
          http.end();

          // delay(5000);
          return true;
        } else {
          Serial.println("Couldn't connect to stacklight");
          return false;
        }
      }
    }
  }

  if (!preferences.isKey("sl_ip")) {
    preferences.end();
    return false;
  }

  Serial.println("Pushing label to Stacklight!");
  WiFiClient client;
  HTTPClient http;
  http.begin(client, (StringSumHelper)"http://" + preferences.getString("sl_ip", "") + ":8080/display");
  http.addHeader("Content-Type", "text/plain");
  int httpResponseCode = http.POST((String)label);
  // http.addHeader("Content-Type", "application/json");
  // int httpResponseCode = http.POST((const StringSumHelper)"{\"label\":\"" + label + "\"}");
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);

    // Check if stacklight is available as AP
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      String SSID = WiFi.SSID(i);
      Serial.print("SSID: ");
      Serial.println(SSID);
      if (SSID == ((const StringSumHelper)"GL_STACKLIGHT_" + preferences.getString("sl_uuid", ""))) {
        // remove saved ip
        preferences.remove("sl_ip");
      }
    }
    WiFi.begin(ssid, password);
    delay(500);
  }
  http.end();
  preferences.end();
  return true;
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