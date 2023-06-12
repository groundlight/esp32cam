/*

Groundlight sample application to post to slack based on the answer to a visual query.  Provided under MIT License below:

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

#include <FS.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <esp_camera.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <sstream>
#include <SPIFFS.h>
#include <ArduinoJson.h>  // https://github.com/bblanchon/ArduinoJson

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Base64.h"

#define FORMAT_SPIFFS_IF_FAILED true

// setting this will allow a GPIO held on boot to reset the wifi credentials and file system
#define GPIO_RESET_CREDENTIALS -1

// set the board type here.  check the pin definitions below to match your actual board
#define CAMERA_MODEL_ESP32_CAM_BOARD 1
// #define CAMERA_MODEL_M5STACK_PSRAM 1

#if defined(CAMERA_MODEL_ESP32_CAM_BOARD)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define GPIO_LED_FLASH 4

#elif defined(CAMERA_MODEL_M5STACK_PSRAM)
#define GPIO_RESET_CREDENTIALS 38

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21
#endif

// Options to configure these settings will be surfaced when the user connects to the device's AP
char groundlight_endpoint[40] = "api.groundlight.ai";
char groundlight_API_key[75] = "api_2R7GfZwHJ5ef7l8zmebTwu5GoPR_a5ZKibaJjgQ2EAYPwKhxcUVg4dsK1QsdPX";
char groundlight_det_name[100] = "det_is_max_working";
char groundlight_det_query[100] = "Is max working?";
char groundlight_det_confidence[5] = "0.9";  // 90% confidence for queries [0.5 - 1.0]
char groundlight_take_action_on[6] = "YES";  // YES or NO
char groundlight_action_channel[10] = "SLACK";
char slack_url[100] = "https://hooks.slack.com/services/blah/blah/blah";
char delay_between_queries_ms[10] = "30000";

camera_fb_t *frame = NULL;
bool actionSaveConfig = false;
bool resetParameters = false;

float targetConfidence = atof(groundlight_det_confidence);
int failures_before_restart = 5;

void check_excessive_failures() {
  failures_before_restart -= 1;
  if (failures_before_restart == 0) {
    Serial.println("Too many failures! Restarting!");
    delay(2000);
    ESP.restart();
  }
}

void saveConfigCallback() {
  Serial.println("Setting flag to save config");
  actionSaveConfig = true;
}

bool loadParams() {
  if (SPIFFS.exists("/config.json")) {
    // file exists, reading and loading
    Serial.println("reading config file");
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      Serial.println("opened config file");
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);

      configFile.readBytes(buf.get(), size);
      DynamicJsonDocument json(1024);
      auto deserializeError = deserializeJson(json, buf.get());
      Serial.println("Parsing json...");
      serializeJson(json, Serial);
      if (!deserializeError) {
        strcpy(groundlight_endpoint, json["endpoint"]);
        strcpy(groundlight_API_key, json["API_key"]);
        strcpy(groundlight_det_name, json["det_name"]);
        strcpy(groundlight_det_query, json["det_query"]);
        strcpy(groundlight_det_confidence, json["det_confidence"]);
        strcpy(groundlight_take_action_on, json["action_on"]);
        strcpy(groundlight_action_channel, json["action"]);
        strcpy(slack_url, json["slack_url"]);
        strcpy(delay_between_queries_ms, json["delay_between_queries_ms"]);
      } else {
        Serial.println("Failed to parse json config!");
        return false;
      }
      configFile.close();
      return true;
    }
  } else {
    return false;  // parameter file does not exist
  }

  return true;
}

bool saveParams() {
  Serial.println("Saving parameters... ");
  DynamicJsonDocument json(1024);
  json["endpoint"] = groundlight_endpoint;
  json["API_key"] = groundlight_API_key;
  json["det_name"] = groundlight_det_name;
  json["det_query"] = groundlight_det_query;
  json["det_confidence"] = groundlight_det_confidence;
  json["action_on"] = groundlight_take_action_on;
  json["action"] = groundlight_action_channel;
  json["slack_url"] = slack_url;
  json["delay_between_queries_ms"] = delay_between_queries_ms;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
    return false;
  }

  serializeJson(json, Serial);
  serializeJson(json, configFile);
  configFile.close();

  return true;
}

void flashLED(int ms, int repeats) {

#if defined(GPIO_LED_FLASH)
  for (int i = 0; i < repeats; i++) {
    digitalWrite(GPIO_LED_FLASH, HIGH);
    delay(ms);
    digitalWrite(GPIO_LED_FLASH, LOW);

    // no blanking delay on the last flash
    if (i < (repeats - 1)) {
      delay(ms);
    }
  }
#endif
}

void setup() {

  if (GPIO_RESET_CREDENTIALS >= 0) {
    pinMode(GPIO_RESET_CREDENTIALS, INPUT_PULLDOWN);
    resetParameters = (digitalRead(GPIO_RESET_CREDENTIALS) == HIGH);
  }

#if defined(GPIO_LED_FLASH)
  pinMode(GPIO_LED_FLASH, OUTPUT);
  digitalWrite(GPIO_LED_FLASH, LOW);
#endif

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // disable brownout detector maybe not necessary

  Serial.begin(115200);
  Serial.println("Edgelight waking up...");

  // use the ESP32 ChipID as an identifier for the Wifi AP
  uint32_t likely_unique_ID = 0;
  for (int i = 0; i < 17; i += 8) {
    uint8_t current_byte = (ESP.getEfuseMac() >> (40 - i)) & 0xff;
    likely_unique_ID |= current_byte << i;
  }

  char edgelight_AP_name[40];
  sprintf(edgelight_AP_name, "Edgelight Config AP 0x%x", likely_unique_ID);
  Serial.printf("AP ID = 0x%x\n", likely_unique_ID);

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Photo Quality Settings
  config.frame_size = FRAMESIZE_UXGA;  // See here for a list of options and resolutions: https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h#L84
  config.jpeg_quality = 10;            // lower means higher quality
  config.fb_count = 2;

  esp_err_t error_code = esp_camera_init(&config);
  if (error_code != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", error_code);
    Serial.println("Restarting system!");
    flashLED(100, 2);
    delay(3000);
    ESP.restart();  // some boards are less reliable for initialization and will everntually just start working
    return;
  }

  Serial.print("Mounting File System...");

  if (SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("Success!");
  } else {
    Serial.println("failed to mount file System!");
    // this is annoying but not fatal
  }

  if (resetParameters) {
    Serial.println("ignoring file system parameters");
  } else if (loadParams()) {
    Serial.println("parameters loaded from filesystem");
  } else {
    Serial.println("failed to load existing parameters from filesystem");
  }

  WiFiManagerParameter custom_groundlight_endpoint("endpoint", "groundlight_endpoint", groundlight_endpoint, 40);
  WiFiManagerParameter custom_groundlight_API_key("API_token", "API token", groundlight_API_key, 75);
  WiFiManagerParameter custom_groundlight_det_name("detector_name", "detector name or id", groundlight_det_name, 100);
  WiFiManagerParameter custom_groundlight_det_query("detector_query", "detector query text", groundlight_det_query, 100);
  WiFiManagerParameter custom_groundlight_det_confidence("detector_confidence", "detector confidence", groundlight_det_confidence, 5);
  WiFiManagerParameter custom_groundlight_take_action_on("action_trigger", "take action on (YES/NO/UNSURE)", groundlight_take_action_on, 6);
  WiFiManagerParameter custom_groundlight_action("action", "channel for action", groundlight_action_channel, 10);
  WiFiManagerParameter custom_slack_url("slack_url", "slack url", slack_url, 100);
  WiFiManagerParameter custom_delay_between_queries_ms("delay_between_queries_ms", "delay between queries (ms)", delay_between_queries_ms, 10);

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);

  wm.addParameter(&custom_groundlight_endpoint);
  wm.addParameter(&custom_groundlight_API_key);
  wm.addParameter(&custom_groundlight_det_name);
  wm.addParameter(&custom_groundlight_det_query);
  wm.addParameter(&custom_groundlight_det_confidence);
  wm.addParameter(&custom_groundlight_take_action_on);
  wm.addParameter(&custom_groundlight_action);
  wm.addParameter(&custom_slack_url);
  wm.addParameter(&custom_delay_between_queries_ms);

  if (resetParameters) {
    Serial.println("Reset button activated.  Forcing launch of wifi manager to reset network");
    wm.resetSettings();
    flashLED(250, 5);
  }

  wm.setBreakAfterConfig(true);
  if (!wm.autoConnect(edgelight_AP_name, "edgelight")) {
    Serial.println("Failed to connect to WiFi with user-provided settings");
    Serial.println("Rebooting in 3 seconds");
    delay(3000);
    ESP.restart();
    delay(5000);
    Serial.println("We should have rebooted before printing this line.  Something is wrong.");
  }

  // save the custom parameters to FS
  if (actionSaveConfig) {
    strcpy(groundlight_endpoint, custom_groundlight_endpoint.getValue());
    strcpy(groundlight_API_key, custom_groundlight_API_key.getValue());
    strcpy(groundlight_det_name, custom_groundlight_det_name.getValue());
    strcpy(groundlight_det_query, custom_groundlight_det_query.getValue());
    strcpy(groundlight_det_confidence, custom_groundlight_det_confidence.getValue());
    strcpy(groundlight_take_action_on, custom_groundlight_take_action_on.getValue());
    strcpy(groundlight_action_channel, custom_groundlight_action.getValue());
    strcpy(slack_url, custom_slack_url.getValue());
    strcpy(delay_between_queries_ms, custom_delay_between_queries_ms.getValue());
    saveParams();
  }

  // indicate we are done with setup
  flashLED(100, 3);
  Serial.println("Setup complete.  Now the party really begins!");

  Serial.printf("using detector : %s\n", groundlight_det_name);

  // generally avoided to keep from leaking an API key over the serial port but sometimes helpful for debugging
  // Serial.printf("using api key : %s\n", groundlight_API_key);

  targetConfidence = atof(groundlight_det_confidence);

  if (!adjust_confidence(groundlight_endpoint, groundlight_det_name, targetConfidence + 0.01, groundlight_API_key)) {
    Serial.println("unable to adjust detector confidence.  restarting to try again!  no point in continuing if we can't get to the GL server!");
    ESP.restart();
    delay(2000);
  }
}

String queryResults = "NONE_YET";
char label[10] = "NONE";
char posicheck_id[50] = "chk_xxx";

int retryLimit = 30;
int retries = 0;
String answer = "NONE";
String prevAnswer = "NONE";
float confidence = 0.0;

void loop() {
  // get image from camera into a buffer
  flashLED(50, 1);
  frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("Camera capture failed!  Restarting system!");
    delay(3000);
    ESP.restart();  // maybe this will fix things?  hard to say. its not going to be worse
  }

  Serial.printf("Captured image.  Encoded size is %d bytes\n", frame->len);

  queryResults = submit_image_query(frame, groundlight_endpoint, groundlight_det_name, groundlight_API_key);
  get_query_id(queryResults).toCharArray(posicheck_id, 50);

  retries = 0;
  while (get_query_confidence(queryResults) < targetConfidence) {
    queryResults = get_image_query(groundlight_endpoint, posicheck_id, groundlight_API_key);

    retries = retries + 1;
    if (retries > retryLimit) {
      break;
    }
  }

  answer = get_query_label(queryResults);
  confidence = get_query_confidence(queryResults);

  if (confidence >= targetConfidence) {
    if (answer == "PASS") {
      if (answer == prevAnswer) {
        Serial.println("(nothing new here.");
      } else {
        Serial.println("updating status notification");
        post_slack_update("whatever we were waiting for just happened!");
        prevAnswer = answer;
      }
    } else if (answer == "FAIL") {
      if (answer == prevAnswer) {
        Serial.println("nothing new here.");
      } else {
        post_slack_update("waiting for the thing to happen again!");
      }
      prevAnswer = answer;
    }
    Serial.print("Confident answer : ");
  } else {
    Serial.print("Tentative answer : ");
  }

  Serial.println(answer);
  Serial.printf("Confidence : %3.3f\n", confidence);

  esp_camera_fb_return(frame);

  int query_delay = atoi(delay_between_queries_ms);

  Serial.printf("waiting %dms between queries...", query_delay);
  delay(query_delay);
  Serial.println("taking another lap!");
}

String get_query_id(const String &jsonResults) {
  DynamicJsonDocument results(1024);
  auto deserializeError = deserializeJson(results, jsonResults);
  const char *id = results["id"] | "PARSING_FAILURE";
  return String(id);
}

String get_query_label(const String &jsonResults) {
  DynamicJsonDocument results(1024);
  auto deserializeError = deserializeJson(results, jsonResults);
  const char *label = results["result"]["label"] | "PARSING_FAILURE";
  return String(label);
}

float get_query_confidence(const String &jsonResults) {
  DynamicJsonDocument results(1024);
  auto deserializeError = deserializeJson(results, jsonResults);
  float confidence = results["result"]["confidence"] | 0.0;

  return (confidence == 0.0) ? 99.999 : confidence;
}

// Posts a message to a Slack channel.
bool post_slack_update(const char message[]) {
  Serial.print("Posting message to Slack: ");
  Serial.println(message);

  // Create a JSON object for the Slack message.
  DynamicJsonDocument slackData(1024);
  slackData["username"] = "edgelight!";
  slackData["icon_emoji"] = ":camera:";
  JsonObject attachments = slackData.createNestedObject("attachments");
  attachments["color"] = "9733EE";
  JsonObject fields = attachments.createNestedObject("fields");
  fields["title"] = message;
  fields["short"] = "false";

  serializeJson(slackData, Serial);
  Serial.printf("\n(done!)\n");

  // Initialize WiFiClientSecure and HTTPClient.
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient https;

  if (client) {
    client->setInsecure();
    https.setTimeout(10000);

    Serial.print("[HTTPS] Preparing to post to Slack at ");
    Serial.println(slack_url);

    // Start HTTPS connection.
    if (https.begin(*client, slack_url)) {
      Serial.print("Attempting [HTTPS] POST...\n");

      String requestBody;
      serializeJson(slackData, requestBody);

      Serial.print("Request body is ");
      Serial.println(requestBody);

      // Add necessary headers for the POST request.
      https.addHeader("Content-Type", "application/json");
      https.addHeader("Content-Length", String(requestBody.length()));

      // Send the POST request and get the response code.
      int httpsResponseCode = https.POST(requestBody);

      if (httpsResponseCode > 0) {
        Serial.printf("[HTTPS] POST... code: %d\n", httpsResponseCode);
      } else {
        Serial.printf("[HTTPS] POST... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
        Serial.println("(It probably still posted to Slack!)");
      }

      https.end();
    } else {
      Serial.print("Unable to connect to ");
      Serial.println(slack_url);
    }

    delete client;
  } else {
    Serial.println("Unable to create client for HTTPS");
  }

  return true;
}

// Collects an HTTP response using an Arduino-compatible implementation
String collectHttpResponse(WiFiClient &client) {
  int timeoutDuration = 20000;
  long startTime = millis();
  bool isBodyReceived = false;
  String responseBody;
  String responseLine;

  // Keep reading until timeout or a valid response body is received
  while ((startTime + timeoutDuration) > millis()) {
    Serial.print(".");
    delay(200);
    while (client.available()) {
      char receivedChar = client.read();

      // Check for a new line in the response
      if (receivedChar == '\n') {
        if (responseLine.length() == 0) {
          isBodyReceived = true;
        }
        responseLine = "";
      } else if (receivedChar != '\r') {
        responseLine += String(receivedChar);
      }

      // Append the received character to the response body if needed
      if (isBodyReceived) {
        responseBody += String(receivedChar);
      }
      startTime = millis();
    }

    if (responseBody.length() > 0) {
      break;
    }
  }

  Serial.println();

  Serial.println("RESPONSE BODY : " + responseBody);

  return responseBody;
}

String submit_image_query(camera_fb_t *image_bytes, char *endpoint, char *detector_id, char *api_token) {
  WiFiClientSecure client;
  String responseBody;

  client.setInsecure();

  if (!client.connect(endpoint, 443)) {
    Serial.println("SSL connection failure!");
    check_excessive_failures();
    return "{ \"result\" : { \"confidence\" : 0.0, \"label\" : \"QUERY_FAIL\" }";
  } else {
    Serial.println("SSL client connected");
  }
  client.setTimeout(120);

  char requestTarget[256] = "POST /device-api/v1/image-queries?detector_id=";
  strcat(requestTarget, detector_id);
  strcat(requestTarget, " HTTP/1.1");

  Serial.printf("attempting image query with [HTTPS] %s\n", requestTarget);
  client.println(requestTarget);
  client.print("Host: ");
  client.println(endpoint);
  client.print("Content-Length: ");
  client.println(image_bytes->len);
  client.println("Content-Type: image/jpeg");
  client.print("X-API-Token: ");
  client.println(api_token);
  client.print("\r\n");

  if (!client.connected()) {
    Serial.println("SSL appears to be dead. returning QUERY_FAIL");
    check_excessive_failures();
    return "{ \"result\" : { \"confidence\" : 0.0, \"label\" : \"QUERY_FAIL\" }";
  }

  uint8_t *image = image_bytes->buf;
  size_t image_size = image_bytes->len;
  int chunk_size = 1024;

  Serial.printf("Writing %d byte image in %d-byte chunks", image_size, chunk_size);

  for (size_t n = 0; n < image_size; n += chunk_size) {
    if (n + chunk_size < image_size) {
      client.write(image, chunk_size);
      Serial.print(".");
      image += chunk_size;
    } else if (image_size % chunk_size > 0) {
      size_t rem = image_size % chunk_size;
      client.write(image, rem);
      Serial.printf(".");
    }
  }
  Serial.println("done!");

  client.print("\r\n");

  if (client.connected()) {
    Serial.print("collecting response...");
    String responseBody = collectHttpResponse(client);
    client.stop();
    return responseBody;
  } else {
    Serial.println("SSL appears to be dead.  returning QUERY_FAIL");
    check_excessive_failures();
    return "{ \"result\" : { \"confidence\" : 0.0, \"label\" : \"QUERY_FAIL\" }";
  }
}

String get_image_query(char *endpoint, char *query_id, char *api_token) {
  /*
    char url[128] = "https://";
    strcat(url, endpoint);
    strcat(url, "/device-api/v1/image-queries/");
    strcat(url, query_id);
  */
  String url = "https://" + String(endpoint) + "/device-api/v1/image-queries/" + String(query_id);
  String response = "NONE";

  WiFiClientSecure *client = new WiFiClientSecure;

  Serial.print("Checking for image query results...");

  if (client) {
    {
      client->setInsecure();
      HTTPClient https;
      https.setTimeout(10000);

      if (https.begin(*client, url)) {

        https.addHeader("X-API-Token", api_token);
        https.addHeader("Content-Type", "application/json");

        int httpsResponseCode = https.GET();

        if (httpsResponseCode > 0) {
          Serial.printf("[HTTPS] response : %d\n", httpsResponseCode);
          response = https.getString();
          Serial.println("response body : " + response);
        } else {
          Serial.printf("[HTTPS] POST... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
          Serial.println("check the logs!");
        }
        https.end();
      } else {
        Serial.print("Unable to connect to ");
        Serial.println(url);
        check_excessive_failures();
      }
    }

    delete client;
  } else {
    Serial.println("Unable to connect to " + String(endpoint));
    check_excessive_failures();
  }

  return response;
}

// Adjusts the confidence threshold for a predictor in the API.
bool adjust_confidence(const char *endpoint, const char *predictorId, float confidence, const char *apiToken) {
  bool success = true;

  // Construct the URL for the PATCH request.
  char url[128] = "https://";
  strcat(url, endpoint);
  strcat(url, "/device-api/predictors/");
  strcat(url, predictorId);

  // Create a JSON object to hold the confidence threshold data.
  DynamicJsonDocument requestData(256);
  requestData["confidence_threshold"] = confidence;

  // Initialize WiFiClientSecure and HTTPClient.
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient https;

  if (client) {
    client->setInsecure();
    https.setTimeout(10000);

    Serial.print("[HTTPS] Preparing send PATCH to Groundlight API at ");
    Serial.println(url);

    // Start HTTPS connection.
    if (https.begin(*client, url)) {
      String requestBody;
      serializeJson(requestData, requestBody);

      Serial.print("Request body is ");
      Serial.println(requestBody);

      // Add necessary headers for the PATCH request.
      https.addHeader("X-API-Token", apiToken);
      https.addHeader("Content-Type", "application/json");
      https.addHeader("Content-Length", String(requestBody.length()));

      // Send the PATCH request and get the response code.
      int httpsResponseCode = https.PATCH(requestBody);

      // Check if the PATCH request was successful.
      if (httpsResponseCode > 0) {
        Serial.printf("[HTTPS] PATCH... code: %d\n", httpsResponseCode);
        Serial.println(https.getString());
      } else {
        Serial.printf("[HTTPS] PATCH... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
        Serial.println("Check the logs!");
        success = false;
      }

      https.end();
    } else {
      Serial.print("Unable to connect to ");
      Serial.println(url);
      success = false;
    }

    delete client;
  } else {
    Serial.println("Unable to create client for HTTPS");
    success = false;
  }

  return success;
}

void wait_forever() {
  int scrap;
  Serial.println("STOPPING HERE! (you can press a key to continue though)");
  while (Serial.available() == 0) {
    continue;
  }
  while (Serial.available()) {
    scrap = Serial.read();
  }
}
