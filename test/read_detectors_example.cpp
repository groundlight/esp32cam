/*

Groundlight basic example of a detector query. Provided under MIT License below:

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
#include "WiFi.h"
// #include "ArduinoJson.h" // https://arduinojson.org/
#include "groundlight.h"
#include "nvs.h"

char groundlight_endpoint[40] = "api.groundlight.ai";
// char groundlight_API_key[75] = "api_yourgroundlightapikeyhere";
// char ssid[40] = "yourssidhere";
// char password[40] = "yourwifipasswordhere";

char groundlight_API_key[75] = "api_2RItZgXp1PihQgDr1otHndZNBBW_7yVnzYdJqkQwRZEtHXyBdqAAEqgyxQv9PD";
char ssid[40] = "Groundlight";
char password[40] = "we-build-robo-brains";

void setup()
{
  Serial.begin(115200);
  // Serial.println("Waking up...");

  WiFi.begin(ssid, password);

  delay(1000);
  String res = get_detectors(groundlight_endpoint, groundlight_API_key);
  // Serial.println(res);

#ifdef HAS_JSON_LIB
  // Serial.println("\nDetectors:");
  detector_list detectors = get_detector_list(groundlight_endpoint, groundlight_API_key);
  for (int i = 0; i < detectors.size; i++)
  {
    // Serial.print("\t");
    // Serial.println(detectors.detectors[i].name);
  }
#endif
}

void loop()
{
  // do nothing
}
