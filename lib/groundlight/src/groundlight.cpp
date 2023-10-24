#include "groundlight.h"
#include "Arduino.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "HTTPClient.h"

// Collects an HTTP response using an Arduino-compatible implementation
String collectHttpResponse(WiFiClient &client)
{
  int timeoutDuration = 20000;
  long startTime = millis();
  bool isBodyReceived = false;
  String responseBody;
  String responseLine;

  // Keep reading until timeout or a valid response body is received
  while ((startTime + timeoutDuration) > millis())
  {
    vTaskDelay(200 / portTICK_PERIOD_MS);
    while (client.available())
    {
      char receivedChar = client.read();

      // Check for a new line in the response
      if (receivedChar == '\n')
      {
        if (responseLine.length() == 0)
        {
          isBodyReceived = true;
        }
        responseLine = "";
      }
      else if (receivedChar != '\r')
      {
        responseLine += String(receivedChar);
      }

      // Append the received character to the response body if needed
      if (isBodyReceived)
      {
        responseBody += String(receivedChar);
      }
      startTime = millis();
    }

    if (responseBody.length() > 0)
    {
      break;
    }
  }

  return responseBody;
}

#ifdef HAS_ESP_CAMERA_LIB
String submit_image_query(camera_fb_t *image_bytes, char *endpoint, char *detector_id, char *api_token)
{
  bool isHTTPS = true;
  String _endpoint = endpoint;
  int port = 443;

  if (_endpoint.indexOf("http://") != -1) {
    isHTTPS = false;
    _endpoint = _endpoint.substring(_endpoint.indexOf("//") + 2);
  } else if (_endpoint.indexOf("https://") != -1) {
    _endpoint = _endpoint.substring(_endpoint.indexOf("//") + 2);
  }

  if (_endpoint.indexOf(":") != -1) {
    port = _endpoint.substring(_endpoint.indexOf(":") + 1).toInt();
    _endpoint = _endpoint.substring(0, _endpoint.indexOf(":"));
  }

  if (isHTTPS) {
    WiFiClientSecure client;
    client.setInsecure();
    return submit_image_query_with_client(image_bytes, _endpoint.c_str(), detector_id, api_token, client, port);
  } else {
    WiFiClient client;
    return submit_image_query_with_client(image_bytes, _endpoint.c_str(), detector_id, api_token, client, port);
  }
}

String submit_image_query_with_client(camera_fb_t *image_bytes, const char *endpoint, char *detector_id, char *api_token, WiFiClient &client, int port)
{
  String responseBody;

  if (!client.connect(endpoint, port)) {
    return "{ \"result\": { \"confidence\": 0.0, \"label\": \"QUERY_FAIL\", \"failure_reason\": \"INITIAL_SSL_CONNECTION_FAILURE\" } }";
  }
  client.setTimeout(120);

  char requestTarget[256] = "POST /device-api/v1/image-queries?detector_id=";
  strcat(requestTarget, detector_id);
  strcat(requestTarget, " HTTP/1.1");

  // Serial.printf("attempting image query with [HTTPS] %s\n", requestTarget);
  client.println(requestTarget);
  client.print("Host: ");
  client.println(endpoint);
  client.print("Content-Length: ");
  client.println(image_bytes->len);
  client.println("Content-Type: image/jpeg");
  client.print("X-API-Token: ");
  client.println(api_token);
  client.print("\r\n");

  if (!client.connected())
  {
    // Serial.println("SSL appears to be dead. returning QUERY_FAIL");
    return "{ \"result\" : { \"confidence\" : 0.0, \"label\" : \"QUERY_FAIL\", \"failure_reason\": \"SSL_CONNECTION_FAILURE\" } }";
  }

  uint8_t *image = image_bytes->buf;
  size_t image_size = image_bytes->len;
  int chunk_size = 1024;

  // Serial.printf("Writing %d byte image in %d-byte chunks", image_size, chunk_size);

  for (size_t n = 0; n < image_size; n += chunk_size)
  {
    if (n + chunk_size < image_size)
    {
      client.write(image, chunk_size);
      // Serial.print(".");
      image += chunk_size;
    }
    else if (image_size % chunk_size > 0)
    {
      size_t rem = image_size % chunk_size;
      client.write(image, rem);
      // Serial.printf(".");
    }
  }
  // Serial.println("done!");

  client.print("\r\n");

  if (client.connected())
  {
    // Serial.print("collecting response...");
    String responseBody = collectHttpResponse(client);
    client.stop();
    if (responseBody.indexOf("Not authenticated.") != -1) {
      return "{ \"result\": { \"confidence\": 0.0, \"label\": \"QUERY_FAIL\", \"failure_reason\": \"NOT_AUTHENTICATED\" } }";
    }
    return responseBody;
  }
  else
  {
    // Serial.println("SSL appears to be dead. returning QUERY_FAIL");
    return "{ \"result\": { \"confidence\": 0.0, \"label\": \"QUERY_FAIL\", \"failure_reason\": \"SSL_CONNECTION_FAILURE_COLLECTING_RESPONSE\" } }";
  }
}
#endif

String get_image_query(char *endpoint, const char *query_id, char *api_token) {
  String url = "https://" + String(endpoint) + "/device-api/v1/image-queries/" + String(query_id);
  String response = "NONE";

  WiFiClientSecure *client = new WiFiClientSecure;

  // Serial.print("Checking for image query results...");

  if (client) {
    
    client->setInsecure();
    HTTPClient https;
    https.setTimeout(10000);

    if (https.begin(*client, url))
    {

      https.addHeader("X-API-Token", api_token);
      https.addHeader("Content-Type", "application/json");

      int httpsResponseCode = https.GET();

      if (httpsResponseCode > 0)
      {
        // Serial.printf("[HTTPS] response : %d\n", httpsResponseCode);
        response = https.getString();
        // Serial.println("response body : " + response);
      }
      else
      {
        // Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
        // Serial.println("check the logs!");
      }
      https.end();
    }
    else {
      // Serial.print("Unable to connect to the Groundlight API");
    }

    delete client;
  }
  else
  {
    // Serial.println("Unable to connect to " + String(endpoint));
  }

  return response;
}

// Adjusts the confidence threshold for a predictor in the API.
bool adjust_confidence(const char *endpoint, const char *predictorId, float confidence, const char *apiToken)
{
  bool success = true;

  // Construct the URL for the PATCH request.
  char url[128] = "https://";
  strcat(url, endpoint);
  strcat(url, "/device-api/predictors/");
  strcat(url, predictorId);

  // Create a JSON object to hold the confidence threshold data.
  // DynamicJsonDocument requestData(256);
  // requestData["confidence_threshold"] = confidence;

  // Initialize WiFiClientSecure and HTTPClient.
  WiFiClientSecure *client = new WiFiClientSecure;
  HTTPClient https;

  if (client)
  {
    client->setInsecure();
    https.setTimeout(10000);

    // Serial.print("[HTTPS] Preparing send PATCH to Groundlight API at ");
    // Serial.println(url);

    // Start HTTPS connection.
    if (https.begin(*client, url))
    {
      // String requestBody;
      // serializeJson(requestData, requestBody);
      String requestBody = "{ \"confidence_threshold\" : " + String(confidence) + " }";

      // Serial.print("Request body is ");
      // Serial.println(requestBody);

      // Add necessary headers for the PATCH request.
      https.addHeader("X-API-Token", apiToken);
      https.addHeader("Content-Type", "application/json");
      https.addHeader("Content-Length", String(requestBody.length()));

      // Send the PATCH request and get the response code.
      int httpsResponseCode = https.PATCH(requestBody);

      // Check if the PATCH request was successful.
      if (httpsResponseCode > 0)
      {
        // Serial.printf("[HTTPS] PATCH... code: %d\n", httpsResponseCode);
        // Serial.println(https.getString());
      }
      else
      {
        // Serial.printf("[HTTPS] PATCH... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
        // Serial.println("Check the logs!");
        success = false;
      }

      https.end();
    }
    else
    {
      // Serial.print("Unable to connect to ");
      // Serial.println(url);
      success = false;
    }

    delete client;
  }
  else
  {
    // Serial.println("Unable to create client for HTTPS");
    success = false;
  }

  return success;
}

// Gets the detectors from the Groundlight API.
String get_detectors(const char *endpoint, const char *apiToken) {
  String url = "https://" + String(endpoint) + "/device-api/v1/detectors";
  String response = "NONE";

  WiFiClientSecure *client = new WiFiClientSecure;

  if (client)
  {
    {
      client->setInsecure();
      HTTPClient https;
      https.setTimeout(10000);

      if (https.begin(*client, url))
      {

        https.addHeader("X-API-Token", apiToken);
        https.addHeader("Content-Type", "application/json");

        int httpsResponseCode = https.GET();

        if (httpsResponseCode > 0)
        {
          response = https.getString();
        }
        else
        {
          // Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
        }
        https.end();
      }
      else
      {
        // Serial.print("Unable to connect to the Groundlight API");
      }
    }

    delete client;
  }
  else
  {
    // Serial.println("Unable to connect to " + String(endpoint));
  }

  return response;
}

#ifdef HAS_JSON_LIB

#ifndef DET_DOC_SIZE
  #define DET_DOC_SIZE 16384
#endif

float get_query_confidence(const String &jsonResults) {
  DynamicJsonDocument results(1024);
  ArduinoJson::DeserializationError deserializeError = deserializeJson(results, jsonResults);
  if (deserializeError != ArduinoJson::DeserializationError::Ok) {
    // Serial.println("Failed to parse JSON");
    return 0.0;
  }
  if (!results.containsKey("result") || !results["result"].containsKey("confidence")) {
    // Serial.println("No result found in JSON");
    return 0.0;
  }
  float confidence = results["result"]["confidence"] | 0.0;

  return (confidence == 0.0) ? 99.999 : confidence;
}

String get_query_id(const String &jsonResults) {
  DynamicJsonDocument results(1024);
  ArduinoJson::DeserializationError deserializeError = deserializeJson(results, jsonResults);
  if (deserializeError != ArduinoJson::DeserializationError::Ok) {
    // Serial.println("Failed to parse JSON");
    return "NONE";
  }
  if (!results.containsKey("id")) {
    // Serial.println("No query ID found in JSON");
    return "NONE";
  }
  return results["id"];
}

StaticJsonDocument<DET_DOC_SIZE> groundlight_json_doc;

// Parses the detectors from the Groundlight API.
detector_list get_detector_list(const char *endpoint, const char *apiToken) {
  deserializeJson(groundlight_json_doc, get_detectors(endpoint, apiToken));
  JsonArray detectors = groundlight_json_doc["results"];
  detector *_detector_list = new detector[detectors.size()];
  for (int i = 0; i < detectors.size(); i++)
  {
    _detector_list[i].confidence_threshold = detectors[i]["confidence_threshold"];
    strcpy(_detector_list[i].id, detectors[i]["id"]);
    strcpy(_detector_list[i].type, detectors[i]["type"]);
    strcpy(_detector_list[i].created_at, detectors[i]["created_at"]);
    strcpy(_detector_list[i].name, detectors[i]["name"]);
    strcpy(_detector_list[i].query, detectors[i]["query"]);
    strcpy(_detector_list[i].group_name, detectors[i]["group_name"]);
  }
  detector_list res = { _detector_list, detectors.size() };
  return res;
}

String detector_to_string(detector d) {
  String res = "Detector: ";
  res += d.id;
  res += "\n\tName: ";
  res += d.name;
  res += "\n\tType: ";
  res += d.type;
  res += "\n\tCreated at: ";
  res += d.created_at;
  res += "\n\tQuery: ";
  res += d.query;
  res += "\n\tGroup name: ";
  res += d.group_name;
  res += "\n\tConfidence threshold: ";
  res += d.confidence_threshold;
  return res;
}

detector get_detector_by_id(const char *endpoint, const char *detectorId, const char *apiToken) {
  detector_list detectors = get_detector_list(endpoint, apiToken);
  for (int i = 0; i < detectors.size; i++) {
    if (String(detectors.detectors[i].id) == String(detectorId)) {
      return detectors.detectors[i];
    }
  }
  return detector { "NONE", "NONE", "NONE", "NONE", "NONE", "NONE", 0.0 };
}
#endif