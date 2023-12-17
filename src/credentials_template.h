#include <Preferences.h>


/*  template file for preloading credentials instead of reading from the file system 
 *  only applicable for [env:demo-unit-preloaded]
 *
 *  to use this file, copy this template to "credentials.h" and uncomment/fill in the applicable values.  
 *  the list below is non-exhaustive and additional options can be added as needed.
 *  this will override anything in the file system on every startup.
*/


void set_preferences(Preferences preferences);

void set_preferences(Preferences preferences) {
    
    preferences.begin("config");
    
    // A non-exhaustive list of preferences for the ESP32
    // preferences.putString("ssid", "");
    // preferences.putString("password", "");
    // preferences.putString("det_id", "");
    // preferences.putString("api_key", "");
    // preferences.putInt("query_delay", 600);
    // preferences.putString("email", "");
    // preferences.putString("emailEndpoint", "");
    // preferences.putString("emailKey", "");
    // preferences.putString("emailHost", "");
    
    preferences.end();
}

