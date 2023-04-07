# esp32cam-edgelight
ESP32 Camera sample app for Groundlight API

The camera will take pictures on defined intervals, send to the Groundlight.ai API over wifi, and send a notification via Slack when the camera first answers "YES", and then as the state changes.

On first boot the device will launch access point to set wifi credentials, detector, slack hook, etc.  Holding a button during reset or power-on will force an erase of the file system and reset this process.   Serial port is configured to 115200 baud for debug messages.  

Works well with M5Stack ESP32 Camera, ESP32CAM, and likely others but would need to verify and update the pinouts.  Some hardware devices are more reliable than others and the software makes regular attempts to reboot as necessary which often gets things going again.

To build and deploy, install the Arduino IDE 2.0, ESP32 board package, and upload to target board.
        
        These instructions are excellent.  https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-mac-and-linux-instructions/

        You will also need to install the WifiManager (https://github.com/tzapu/WiFiManager) and ArduinoJson (https://github.com/bblanchon/ArduinoJson) libraries


Note that API key is stored in plaintext in the file system on the ESP32.  Please generate a dedicated key for your device if possible. 

Currently only supports slack web hooks for notification, but easily extensible to other options, https://api.slack.com/messaging/webhooks
