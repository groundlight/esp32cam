#include <Arduino.h>
#include "WiFi.h"
#include <HTTPClient.h>

namespace Stacklight
{
    String stacklight_ap_ip = "http://192.168.4.1:8080";

    bool isStacklightAPAvailable(String stacklightId) {
        WiFi.disconnect();
        // delay(100);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; i++) {
            String SSID = WiFi.SSID(i);
            if (SSID == ((const StringSumHelper) "GL_STACKLIGHT_" + stacklightId)) {
                // Serial.print("Found Stacklight with SSID: ");
                // Serial.println(SSID);
                return true;
            }
        }
        // Serial.println("Stacklight AP not available");
        return false;
    }

    bool pushLabelToStacklight(const char *ip, const char *label, bool switchColors = false) {
        // Serial.println("Pushing label to Stacklight!");
        WiFiClient client;
        HTTPClient http;
        http.begin(client, (StringSumHelper) "http://" + ip + ":8080/display");
        http.addHeader("Content-Type", "text/plain");
        String labelStr = (String)label;
        labelStr.toUpperCase();
        if (switchColors) {
            labelStr = labelStr == "PASS" || labelStr == "YES" ? "FAIL" : "PASS";
        }
        int httpResponseCode = http.POST(labelStr);
        // http.addHeader("Content-Type", "application/json");
        // int httpResponseCode = http.POST((const StringSumHelper)"{\"label\":\"" + label + "\"}");
        bool success = false;
        if (httpResponseCode > 0) {
            // Serial.print("HTTP Response code: ");
            // Serial.println(httpResponseCode);
            String payload = http.getString();
            // Serial.println(payload);
            success = true;
        }
        else {
            // Serial.print("Error code: ");
            // Serial.println(httpResponseCode);
        }
        http.end();
        client.flush();
        client.stop();
        return success;
    }

    bool pushWiFiCredToStacklight(const char * ssid, const char * password) {
        WiFiClient client;
        HTTPClient http;
        http.begin(client, stacklight_ap_ip);

        for (int i = 0; i < 40 && !http.connected(); i++) {
            // delay(100);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST((const StringSumHelper) "{\"ssid\":\"" + ssid + "\",\"password\":\"" + password + "\"}");
        // Serial.print("HTTP Response code: ");
        // Serial.println(httpResponseCode);
        if (httpResponseCode < 0) {
            // Serial.print("Error: ");
            // Serial.println(http.errorToString(httpResponseCode));
        }
        String payload = http.getString();
        http.end();
        client.flush();
        client.stop();
        return httpResponseCode == HTTP_CODE_OK && payload == "OK";
    }

    String getStacklightIP() {
        WiFiClient client;
        HTTPClient http;
        String ip = "";
        http.setReuse(true);

        http.begin((StringSumHelper)stacklight_ap_ip + "/ip");
        for (int i = 0; i < 40 && !http.connected(); i++) {
            // delay(100);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        int httpResponseCode = http.GET();

        if (httpResponseCode > 0 && httpResponseCode == HTTP_CODE_OK) {
            // Serial.print("HTTP Response code: ");
            // Serial.println(httpResponseCode);
            String payload = http.getString();
            if (payload != "Not connected to WiFi") {
                // Serial.println(payload);
                ip = payload;
            }
        }
        else {
            // Serial.print("Error code: ");
            // Serial.print(httpResponseCode);
            // Serial.print(" [");
            // Serial.print(http.errorToString(httpResponseCode));
            // Serial.println("]");
        }
        http.end();
        client.flush();
        client.stop();
        return ip;
    }

    /**
     * @brief Try to connect to Stacklight AP and push WiFi credentials
     * 
     * This happens over time, and not for initialization. Must be connected to the stacklight first.
     * 
     * @param ssid WiFi SSID
     * @param password WiFi password
     * 
     * @return Stacklight IP address (empty string if not connected)
    */
    String tryConnectToStacklight(const char * ssid, const char * password) {
        String ip = getStacklightIP();

        if (ip == "") {
            // delay(1000);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            pushWiFiCredToStacklight(ssid, password);
        }
        
        return ip;
    }

    /**
     * @brief Try to connect to Stacklight AP and push WiFi credentials
     * 
     * This is for initialization. Must be connected to the stacklight first.
     * 
     * @param ssid WiFi SSID
     * @param password WiFi password
     * 
     * @return Stacklight IP address (empty string if not connected)
    */
    String initStacklight(const char * ssid, const char * password, int retries = 10) {
        if (!WiFi.isConnected()) {
            // Serial.println("Not connected to WiFi");
            return "";
        }

        bool stacklight_cred_pushed = false;

        for (int i = 0; i < retries; i++) {
            if (pushWiFiCredToStacklight(ssid, password)) {
                stacklight_cred_pushed = true;
                break;
            }
            // delay(500);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        if (!stacklight_cred_pushed) {
            // Serial.println("Failed to push WiFi credentials to Stacklight");
            return "";
        }

        // delay(1000);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        String ip = "";

        for (int i = 0; i < retries && ip == ""; i++) {
            ip = getStacklightIP();
            if (ip == "") {
                // delay(500);
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
        }
        return ip;
    }
}
