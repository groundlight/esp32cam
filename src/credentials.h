#include <Preferences.h>

void set_preferences(Preferences preferences);
void set_preferences(Preferences preferences) {
    // A non-exhaustive list of preferences for the ESP32
    preferences.begin("config");
    preferences.putString("ssid", "");
    preferences.putString("password", "");
    preferences.putString("det_id", "");
    preferences.putString("api_key", "");
    preferences.putInt("query_delay", 600);
    // preferences.putString("email", "");
    // preferences.putString("emailEndpoint", "");
    // preferences.putString("emailKey", "");
    // preferences.putString("emailHost", "");
    preferences.end();
}

