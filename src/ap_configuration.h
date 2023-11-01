// Adapted from https://github.com/CDFER/Captive-Portal-ESP32
// Thank you to Chris (https://github.com/CDFER) for figuring out Captive Portal on ESP32!

#include <Arduino.h>  //not needed in the arduino ide

// Captive Portal
#include <AsyncTCP.h>  //https://github.com/me-no-dev/AsyncTCP using the latest dev version from @me-no-dev
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>	//https://github.com/me-no-dev/ESPAsyncWebServer using the latest dev version from @me-no-dev
#include <esp_wifi.h>			//Used for mpdu_rx_disable android workaround
#include <Preferences.h>

// Pre reading on the fundamentals of captive portals https://textslashplain.com/2022/06/24/captive-portals/

#define MAX_CLIENTS 4	// ESP32 supports up to 10 but I have not tested it yet
#define WIFI_CHANNEL 6	// 2.4ghz channel 6 https://en.wikipedia.org/wiki/List_of_WLAN_channels#2.4_GHz_(802.11b/g/n/ax)

const IPAddress localIP(4, 3, 2, 1);		   // the IP address the web server, Samsung requires the IP to be in public space
const IPAddress gatewayIP(4, 3, 2, 1);		   // IP address of the network should be the same as the local IP for captive portals
const IPAddress subnetMask(255, 255, 255, 0);  // no need to change: https://avinetworks.com/glossary/subnet-mask/

const String localIPURL = "http://4.3.2.1";	 // a string version of the local IP with http, used for redirecting clients to your webpage

Preferences preferences;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    form {
      display: flex;
      flex-direction: column;
      place-items: center normal;
    }
    input {
      width: 80vw;
      margin: 10px;
    }
    input[type=submit] {
      width: 100px;
      margin: 10px;
      background-color: #4CAF50;
      color: white;
      padding: 14px 20px;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    input[type=submit]:hover {
      background-color: #45a049;
    }
  </style>
  </head><body>
  <form action="/config">
    WiFi SSID: <input type="text" name="ssid" value="%ssid%">
    WiFi Password: <input type="text" name="pw" value="%password%">
    Detector Id: <input type="text" name="det_id" value="%det_id%">
    API Key: <input type="text" name="api_key" value="%api_key%">
    Query Delay (seconds): <input type="text" name="query_delay" value="%query_delay%">
    Endpoint: <input type="text" name="endpoint" value="%endpoint%">
    Target Confidence: <input type="text" name="tConf" value="%tConf%">
    Motion Alpha (float between 0 and 1): <input type="text" name="mot_a" value="%mot_a%">
    Motion Beta (float between 0 and 1): <input type="text" name="mot_b" value="%mot_b%">
    Stacklight UUID: <input type="text" name="sl_uuid" value="%sl_uuid%">
    Slack URL: <input type="text" name="slack_url" value="%slack_url%">
    Email: <input type="text" name="email" value="%email%">
    Email Endpoint: <input type="text" name="email_endpoint" value="%email_endpoint%">
    Email Key: <input type="text" name="email_key" value="%email_key%">
    Email Host: <input type="text" name="email_host" value="%email_host%">
    Twilio SID: <input type="text" name="twilio_sid" value="%twilio_sid%">
    Twilio Token: <input type="text" name="twilio_token" value="%twilio_token%">
    Twilio Number: <input type="text" name="twilio_number" value="%twilio_number%">
    Twilio Recipient: <input type="text" name="twilio_recipient" value="%twilio_recipient%">
    <input type="submit" value="Submit">
  </form>
</body></html>
)rawliteral";

const char sent_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    h1 {
      font-size: 2em;
    }
  </style>
  </head><body>
  <h1>Configuration Sent!</h1>
  <a href="/">Return to Home Page</a>
</body></html>
)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

String processor(const String& var) {
  preferences.begin("config");
  String out = "";
  if (var == "ssid") out = preferences.getString("ssid", "");
  else if (var == "password") out = preferences.getString("password", "");
  else if (var == "det_id") out = preferences.getString("det_id", "");
  else if (var == "api_key") out = preferences.getString("api_key", "");
  else if (var == "query_delay") out = String(preferences.getInt("query_delay", 30));
  else if (var == "endpoint") out = preferences.getString("endpoint", "");
  else if (var == "tConf") out = preferences.getString("tCStr", "0.9");
  else if (var == "mot_a" && preferences.isKey("mot_a")) out = String(preferences.getString("mot_a", "0.0"));
  else if (var == "mot_b" && preferences.isKey("mot_b")) out = String(preferences.getString("mot_b", "0.0"));
  else if (var == "sl_uuid" && preferences.isKey("sl_uuid")) out = preferences.getString("sl_uuid", "");
  else if (var == "slack_url" && preferences.isKey("slack_url")) out = preferences.getString("slack_url", "");
  else if (var == "email" && preferences.isKey("email")) out = preferences.getString("email", "");
  else if (var == "email_endpoint" && preferences.isKey("emailEndpoint")) out = preferences.getString("emailEndpoint", "");
  else if (var == "email_key" && preferences.isKey("emailKey")) out = preferences.getString("emailKey", "");
  else if (var == "email_host" && preferences.isKey("emailHost")) out = preferences.getString("emailHost", "");
  else if (var == "twilio_sid" && preferences.isKey("twilioSID")) out = preferences.getString("twilioSID", "");
  else if (var == "twilio_token" && preferences.isKey("twilioKey")) out = preferences.getString("twilioKey", "");
  else if (var == "twilio_number" && preferences.isKey("twilioNumber")) out = preferences.getString("twilioNumber", "");
  else if (var == "twilio_recipient" && preferences.isKey("twilioEndpoint")) out = preferences.getString("twilioEndpoint", "");
  preferences.end();
  return out;
}

DNSServer dnsServer;
AsyncWebServer server(80);

void setUpDNSServer(DNSServer &dnsServer, const IPAddress &localIP) {
// Define the DNS interval in milliseconds between processing DNS requests
#define DNS_INTERVAL 30

	// Set the TTL for DNS response and start the DNS server
	dnsServer.setTTL(3600);
	dnsServer.start(53, "*", localIP);
}

void startSoftAccessPoint(String ssid, String password, const IPAddress &localIP, const IPAddress &gatewayIP) {
// Define the maximum number of clients that can connect to the server
#define MAX_CLIENTS 4
// Define the WiFi channel to be used (channel 6 in this case)
#define WIFI_CHANNEL 6

	// Set the WiFi mode to access point and station

	// Define the subnet mask for the WiFi network
	const IPAddress subnetMask(255, 255, 255, 0);

	// Configure the soft access point with a specific IP and subnet mask
	WiFi.softAPConfig(localIP, gatewayIP, subnetMask);
    // vTaskDelay(100 / portTICK_PERIOD_MS);  // Add a small delay

	// Start the soft access point with the given ssid, password, channel, max number of clients
    WiFi.softAP(ssid, password, WIFI_CHANNEL, 0, MAX_CLIENTS);

	// Disable AMPDU RX on the ESP32 WiFi to fix a bug on Android
	esp_wifi_stop();
	esp_wifi_deinit();
	wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
	my_config.ampdu_rx_enable = false;
	esp_wifi_init(&my_config);
	esp_wifi_start();
	vTaskDelay(100 / portTICK_PERIOD_MS);  // Add a small delay
}

void setUpWebserver(AsyncWebServer &server, const IPAddress &localIP) {
	//======================== Webserver ========================
	// WARNING IOS (and maybe macos) WILL NOT POP UP IF IT CONTAINS THE WORD "Success" https://www.esp8266.com/viewtopic.php?f=34&t=4398
	// SAFARI (IOS) IS STUPID, G-ZIPPED FILES CAN'T END IN .GZ https://github.com/homieiot/homie-esp8266/issues/476 this is fixed by the webserver serve static function.
	// SAFARI (IOS) there is a 128KB limit to the size of the HTML. The HTML can reference external resources/images that bring the total over 128KB
	// SAFARI (IOS) popup browser has some severe limitations (javascript disabled, cookies disabled)

	// Required
	server.on("/connecttest.txt", [](AsyncWebServerRequest *request) { request->redirect("http://logout.net"); });	// windows 11 captive portal workaround
	server.on("/wpad.dat", [](AsyncWebServerRequest *request) { request->send(404); });								// Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32 :)

	// Background responses: Probably not all are Required, but some are. Others might speed things up?
	// A Tier (commonly used by modern systems)
	server.on("/generate_204", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });		   // android captive portal redirect
	server.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });			   // microsoft redirect
	server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });  // apple call home
	server.on("/canonical.html", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });	   // firefox captive portal call home
	server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); });					   // firefox captive portal call home
	server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });			   // windows call home

	// B Tier (uncommon)
	//  server.on("/chrome-variations/seed",[](AsyncWebServerRequest *request){request->send(200);}); //chrome captive portal call home
	//  server.on("/service/update2/json",[](AsyncWebServerRequest *request){request->send(200);}); //firefox?
	//  server.on("/chat",[](AsyncWebServerRequest *request){request->send(404);}); //No stop asking Whatsapp, there is no internet connection
	//  server.on("/startpage",[](AsyncWebServerRequest *request){request->redirect(localIPURL);});

	// return 404 to webpage icon
	server.on("/favicon.ico", [](AsyncWebServerRequest *request) { request->send(404); });	// webpage icon

	// Serve Basic HTML Page
	server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html, processor);
		response->addHeader("Cache-Control", "public,max-age=31536000");  // save this file to cache for 1 year (unless you refresh)
		request->send(response);
		Serial.println("Served Basic HTML Page");
	});

    server.on("/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    preferences.begin("config", false);
        if (request->hasParam("ssid") && request->getParam("ssid")->value() != "") {
        preferences.putString("ssid", request->getParam("ssid")->value());
        }
        if (request->hasParam("pw") && request->getParam("pw")->value() != "") {
        preferences.putString("password", request->getParam("pw")->value());
        }
        if (request->hasParam("det_id") && request->getParam("det_id")->value() != "") {
        preferences.putString("det_id", request->getParam("det_id")->value());
        }
        if (request->hasParam("api_key") && request->getParam("api_key")->value() != "") {
        preferences.putString("api_key", request->getParam("api_key")->value());
        }
        if (request->hasParam("query_delay") && request->getParam("query_delay")->value() != "") {
        preferences.putInt("query_delay", 30);
        }
        if (request->hasParam("endpoint") && request->getParam("endpoint")->value() != "") {
        preferences.putString("endpoint", request->getParam("endpoint")->value());
        }
        if (request->hasParam("tConf") && request->getParam("tConf")->value() != "") {
        preferences.putString("tCStr", request->getParam("tConf")->value());
        preferences.putFloat("tConf", request->getParam("tConf")->value().toFloat());
        }
        if (request->hasParam("mot_a") && request->getParam("mot_a")->value() != "") {
        preferences.putString("mot_a", request->getParam("mot_a")->value());
        }
        if (request->hasParam("mot_b") && request->getParam("mot_b")->value() != "") {
        preferences.putString("mot_b", request->getParam("mot_b")->value());
        }
        if (request->hasParam("sl_uuid") && request->getParam("sl_uuid")->value() != "") {
        preferences.putString("sl_uuid", request->getParam("sl_uuid")->value());
        }
        if (request->hasParam("slack_url") && request->getParam("slack_url")->value() != "") {
        preferences.putString("slack_url", request->getParam("slack_url")->value());
        }
        if (request->hasParam("email") && request->getParam("email")->value() != "") {
        preferences.putString("email", request->getParam("email")->value());
        }
        if (request->hasParam("email_endpoint") && request->getParam("email_endpoint")->value() != "") {
        preferences.putString("emailEndpoint", request->getParam("email_endpoint")->value());
        }
        if (request->hasParam("email_key") && request->getParam("email_key")->value() != "") {
        preferences.putString("emailKey", request->getParam("email_key")->value());
        }
        if (request->hasParam("email_host") && request->getParam("email_host")->value() != "") {
        preferences.putString("emailHost", request->getParam("email_host")->value());
        }
        if (request->hasParam("twilio_sid") && request->getParam("twilio_sid")->value() != "") {
        preferences.putString("twilioSID", request->getParam("twilio_sid")->value());
        }
        if (request->hasParam("twilio_token") && request->getParam("twilio_token")->value() != "") {
        preferences.putString("twilioKey", request->getParam("twilio_token")->value());
        }
        if (request->hasParam("twilio_number") && request->getParam("twilio_number")->value() != "") {
        preferences.putString("twilioNumber", request->getParam("twilio_number")->value());
        }
        if (request->hasParam("twilio_recipient") && request->getParam("twilio_recipient")->value() != "") {
        preferences.putString("twilioEndpoint", request->getParam("twilio_recipient")->value());
        }
        request->send_P(200, "text/html", sent_html);
        preferences.end();
    });

	// the catch all
	server.onNotFound([](AsyncWebServerRequest *request) {
		request->redirect(localIPURL);
		Serial.print("onnotfound ");
		Serial.print(request->host());	// This gives some insight into whatever was being requested on the serial monitor
		Serial.print(" ");
		Serial.print(request->url());
		Serial.print(" sent redirect to " + localIPURL + "\n");
	});
}

void dns_listener(void * parameter) {
    while (true) {
        dnsServer.processNextRequest();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void ap_setup(String ssid, String password) {
	startSoftAccessPoint(ssid, password, localIP, gatewayIP);

	setUpDNSServer(dnsServer, localIP);

	setUpWebserver(server, localIP);
	server.begin();

    xTaskCreate(
        dns_listener,         // Function that should be called
        "DNS Listener",  // Name of the task (for debugging)
        10000,            // Stack size (bytes)
        NULL,             // Parameter to pass
        1,                // Task priority
        NULL              // Task handle
    );
}
