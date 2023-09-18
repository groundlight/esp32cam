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

enum QueryState {
  WAITING_TO_QUERY,
  DNS_NOT_FOUND,
  SSL_CONNECTION_FAILURE,
  LAST_RESPONSE_PASS,
  LAST_RESPONSE_FAIL,
  LAST_RESPONSE_UNSURE,
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
// #define FRAME_ARR_LEN ((1600 / 8) * (1200 / 8) * 2)
// #define FRAME_ARR_LEN 1600 * 1200 * 2 / 64
// #define FRAME_ARR_LEN 1280 * 1024 * 3 / 64
#define FRAME_ARR_LEN 1280 * 1024 * 2 / 64
// uint8_t frame_565[FRAME_ARR_LEN];
uint8_t *frame_565;
uint8_t *frame_565_old;
// uint8_t frame_565_old[FRAME_ARR_LEN];
#define ALPHA_DIVISOR 10
#define ALPHA FRAME_ARR_LEN / ALPHA_DIVISOR
#define BETA 20 // out of 31

void setup() {
  Serial.begin(115200);

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

  // alloc memory for 565 frames
  frame_565 = (uint8_t *) ps_malloc(FRAME_ARR_LEN);
  frame_565_old = (uint8_t *) ps_malloc(FRAME_ARR_LEN);

  vTaskDelay(100 / portTICK_PERIOD_MS);
  Serial.println("Camera Initialized");
  vTaskDelay(100 / portTICK_PERIOD_MS);
}

int num_diffs = 0;

bool is_motion_detected(camera_fb_t* frame) {
  bool worked = jpg2rgb565(frame->buf, frame->len, frame_565, JPG_SCALE_8X);
  if (!worked) {
    return false;
  }
  // try to detect motion
  num_diffs = 0;
  for (int i = 0; i < FRAME_ARR_LEN; i += 2) {
    uint16_t color = (frame_565[i] << 8) | frame_565[i + 1];
    uint16_t color_old = (frame_565_old[i] << 8) | frame_565_old[i + 1];
    uint8_t b_diff = abs((color & 0b11111) - (color_old & 0b11111));
    uint8_t g_diff = abs(((color >> 5) & 0b111111) - ((color_old >> 5) & 0b111111));
    uint8_t r_diff = abs(((color >> 11) & 0b11111) - ((color_old >> 11) & 0b11111));
    if (b_diff > BETA || (g_diff >> 1) > BETA || r_diff > BETA) {
      num_diffs++;
    }
  }
  bool motion_detected = num_diffs > ALPHA;

  if (motion_detected) {
    for (int i = 0; i < FRAME_ARR_LEN; i += 2) {
      frame_565_old[i] = frame_565[i];
      frame_565_old[i + 1] = frame_565[i + 1];
    }
  }
}

void loop () {
  frame = esp_camera_fb_get();
  // Testing how to get the latest image
  esp_camera_fb_return(frame);
  frame = esp_camera_fb_get();

  int time = millis();
  bool motion_detected = is_motion_detected(frame);
  int time2 = millis();
  Serial.println((StringSumHelper) "Time to detect motion: " + (time2 - time) + "ms");
  if (motion_detected) {
    debug((StringSumHelper) "Motion detected! " + num_diffs + " diffs");
  } else {
    // debug((StringSumHelper) "No motion detected! " + num_diffs + " diffs");
  }

  if (!frame)
  {
    debug("Camera capture failed! Restarting system!");
    delay(3000);
    ESP.restart(); // maybe this will fix things? hard to say. its not going to be worse
  }

  debug((StringSumHelper) "Captured image. Encoded size is " + frame->len + " bytes");

  esp_camera_fb_return(frame);
}