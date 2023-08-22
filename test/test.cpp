#include <Arduino.h>

void setup()
{
  Serial.begin(115200);
  Serial.println("Edgelight waking up...");
}

String input = "";

void loop()
{
  if (Serial.available() > 0) {
//     input += (char)Serial.read();
//   } else if (input.length() > 0) {
//     delay(1000);
//     Serial.println(input);
//     input = "";
    Serial.println(Serial.read());
  }
}
