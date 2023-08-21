/*

Groundlight basic example of image classification from a visual query. Provided under MIT License below:

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
#include <esp_camera.h>
#include "ArduinoJson.h" // https://arduinojson.org/
#include "groundlight.h"

char groundlight_endpoint[40] = "api.groundlight.ai";
char groundlight_API_key[75] = "api_yourgroundlightapikeyhere";
char groundlight_det_name[100] = "det_xxxyourdetectoridhere";
char groundlight_det_query[100] = "your query text here?";
char groundlight_det_confidence[5] = "0.9"; // 90% confidence for queries [0.5 - 1.0]
char groundlight_take_action_on[6] = "YES"; // YES or NO
char delay_between_queries_ms[10] = "30000";

// set the board type here.  check the pin definitions below to match your actual board
#define CAMERA_MODEL_ESP32_CAM_BOARD
// #define CAMERA_MODEL_M5STACK_PSRAM

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

camera_fb_t *frame = NULL;

float targetConfidence = atof(groundlight_det_confidence);
int failures_before_restart = 5;

String get_query_id(const String &jsonResults);
String get_query_label(const String &jsonResults);
float get_query_confidence(const String &jsonResults);

void check_excessive_failures()
{
  failures_before_restart -= 1;
  if (failures_before_restart == 0)
  {
    Serial.println("Too many failures! Restarting!");
    delay(2000);
    ESP.restart();
  }
}

void flashLED(int ms, int repeats)
{
#if defined(GPIO_LED_FLASH)
  for (int i = 0; i < repeats; i++)
  {
    digitalWrite(GPIO_LED_FLASH, HIGH);
    delay(ms);
    digitalWrite(GPIO_LED_FLASH, LOW);

    // no blanking delay on the last flash
    if (i < (repeats - 1))
    {
      delay(ms);
    }
  }
#endif
}

void setup()
{

#if defined(GPIO_LED_FLASH)
  pinMode(GPIO_LED_FLASH, OUTPUT);
  digitalWrite(GPIO_LED_FLASH, LOW);
#endif

  // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector maybe not necessary

  Serial.begin(115200);
  Serial.println("Edgelight waking up...");

  // use the ESP32 ChipID as an identifier for the Wifi AP
  uint32_t likely_unique_ID = 0;
  for (int i = 0; i < 17; i += 8)
  {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Photo Quality Settings
  config.frame_size = FRAMESIZE_UXGA; // See here for a list of options and resolutions: https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h#L84
  config.jpeg_quality = 10;           // lower means higher quality
  config.fb_count = 2;

  esp_err_t error_code = esp_camera_init(&config);
  if (error_code != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", error_code);
    Serial.println("Restarting system!");
    flashLED(100, 2);
    delay(3000);
    ESP.restart(); // some boards are less reliable for initialization and will everntually just start working
    return;
  }

  // indicate we are done with setup
  flashLED(100, 3);

  Serial.printf("using detector : %s\n", groundlight_det_name);

  // generally avoided to keep from leaking an API key over the serial port but sometimes helpful for debugging
  // Serial.printf("using api key : %s\n", groundlight_API_key);
}

String queryResults = "NONE_YET";
char label[10] = "NONE";
char posicheck_id[50] = "chk_xxx";

int retryLimit = 30;
int retries = 0;
String answer = "NONE";
String prevAnswer = "NONE";
float confidence = 0.0;

void loop()
{
  // get image from camera into a buffer
  flashLED(50, 1);
  frame = esp_camera_fb_get();
  if (!frame)
  {
    Serial.println("Camera capture failed!  Restarting system!");
    delay(3000);
    ESP.restart(); // maybe this will fix things?  hard to say. its not going to be worse
  }

  Serial.printf("Captured image.  Encoded size is %d bytes\n", frame->len);

  queryResults = submit_image_query(frame, groundlight_endpoint, groundlight_det_name, groundlight_API_key);
  get_query_id(queryResults).toCharArray(posicheck_id, 50);

  retries = 0;
  while (get_query_confidence(queryResults) < targetConfidence)
  {
    queryResults = get_image_query(groundlight_endpoint, posicheck_id, groundlight_API_key);

    retries = retries + 1;
    if (retries > retryLimit)
    {
      break;
    }
  }

  answer = get_query_label(queryResults);
  confidence = get_query_confidence(queryResults);

  Serial.println(answer);
  Serial.printf("Confidence : %3.3f\n", confidence);

  esp_camera_fb_return(frame);

  int query_delay = atoi(delay_between_queries_ms);

  Serial.printf("waiting %dms between queries...", query_delay);
  delay(query_delay);
  Serial.println("taking another lap!");
}

String get_query_id(const String &jsonResults)
{
  DynamicJsonDocument results(1024);
  auto deserializeError = deserializeJson(results, jsonResults);
  const char *id = results["id"] | "PARSING_FAILURE";
  return String(id);
}

String get_query_label(const String &jsonResults)
{
  DynamicJsonDocument results(1024);
  auto deserializeError = deserializeJson(results, jsonResults);
  const char *label = results["result"]["label"] | "PARSING_FAILURE";
  return String(label);
}

float get_query_confidence(const String &jsonResults)
{
  DynamicJsonDocument results(1024);
  auto deserializeError = deserializeJson(results, jsonResults);
  float confidence = results["result"]["confidence"] | 0.0;

  return (confidence == 0.0) ? 99.999 : confidence;
}
