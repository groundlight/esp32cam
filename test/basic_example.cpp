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
#include "WiFi.h"
#include "groundlight.h"

// set the board type here. check the pin definitions below to match your actual board
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

char groundlight_endpoint[40] = "api.groundlight.ai";
// char groundlight_API_key[75] = "api_yourgroundlightapikeyhere";
// char groundlight_det_name[100] = "det_xxxyourdetectoridhere";
// char ssid[40] = "yourssidhere";
// char password[40] = "yourwifipasswordhere";

char groundlight_API_key[75] = "api_2RItZgXp1PihQgDr1otHndZNBBW_7yVnzYdJqkQwRZEtHXyBdqAAEqgyxQv9PD";
char groundlight_det_id[100] = "det_2RFxH74RVpgUyStiaQITkw1rp9S";
char ssid[40] = "Groundlight";
char password[40] = "we-build-robo-brains";

String queryResults = "NONE_YET";
int query_delay = 30000; // 30 seconds

void setup()
{

#if defined(GPIO_LED_FLASH)
  pinMode(GPIO_LED_FLASH, OUTPUT);
  digitalWrite(GPIO_LED_FLASH, LOW);
#endif

  Serial.begin(115200);
  Serial.println("Edgelight waking up...");

  WiFi.begin(ssid, password);

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
    delay(3000);
    ESP.restart(); // some boards are less reliable for initialization and will everntually just start working
    return;
  }

  Serial.printf("using detector : %s\n", groundlight_det_id);

  delay(2000);
}

void loop()
{
  // get image from camera into a buffer
  #if defined(GPIO_LED_FLASH)
    digitalWrite(GPIO_LED_FLASH, HIGH);
    delay(1000);
  #endif

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

  queryResults = submit_image_query(frame, groundlight_endpoint, groundlight_det_id, groundlight_API_key);
  Serial.println("Query Results:");
  Serial.println(queryResults);
  Serial.println();

  esp_camera_fb_return(frame);

  Serial.printf("waiting %dms between queries...", query_delay);
  delay(query_delay);
  Serial.println("taking another lap!");
}
