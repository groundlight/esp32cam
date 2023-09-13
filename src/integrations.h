// #pragma once

#include <Arduino.h>
#include <ESP_Mail_Client.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "twilio.hpp"

/** The smtp host name e.g. smtp.gmail.com for GMail or smtp.office365.com for Outlook or smtp.mail.yahoo.com */
// #define SMTP_HOST "<host>"

/** The smtp port e.g.
 * 25  or esp_mail_smtp_port_25
 * 465 or esp_mail_smtp_port_465
 * 587 or esp_mail_smtp_port_587
 */
// #define SMTP_PORT esp_mail_smtp_port_587

/* The log in credentials */
// #define AUTHOR_EMAIL "<email>"
// #define AUTHOR_PASSWORD "<password>"

/* Recipient email address */
// #define RECIPIENT_EMAIL "<recipient email here>"

/* Declare the global used SMTPSession object for SMTP transport */
SMTPSession smtp;

/* Callback function to get the Email sending status */
// void smtpCallback(SMTP_Status status);

Twilio *twilio;

void sendSlackNotification(String detectorName, String query, String key, String endpoint, String label, camera_fb_t *fb) {
    // // Serial.print("Posting message to Slack: ");
    // // Serial.println(message);

    // Create a JSON object for the Slack message.
    DynamicJsonDocument slackData(1024);
    slackData["username"] = "edgelight!";
    slackData["icon_emoji"] = ":camera:";
    JsonObject attachments = slackData.createNestedObject("attachments");
    attachments["color"] = "9733EE";
    JsonObject fields = attachments.createNestedObject("fields");
    // fields["title"] = "Detector (" + detectorName + ") detected " + label + "!";
    char title[100];
    sprintf(title, "Detector (%s) detected %s to the question (%s)!\n", detectorName.c_str(), label.c_str(), query.c_str());
    fields["title"] = title;
    fields["short"] = "false";

    serializeJson(slackData, Serial);
    // Serial.printf("\n(done!)\n");

    // Initialize WiFiClientSecure and HTTPClient.
    WiFiClientSecure *client = new WiFiClientSecure;
    HTTPClient https;

    if (client)
    {
        client->setInsecure();
        https.setTimeout(10000);

        // Serial.print("[HTTPS] Preparing to post to Slack at ");
        // Serial.println(endpoint);

        // Start HTTPS connection.
        if (https.begin(*client, endpoint))
        {
        // Serial.print("Attempting [HTTPS] POST...\n");

        String requestBody;
        serializeJson(slackData, requestBody);

        // Serial.print("Request body is ");
        // Serial.println(requestBody);

        // Add necessary headers for the POST request.
        https.addHeader("Content-Type", "application/json");
        https.addHeader("Content-Length", String(requestBody.length()));

        // Send the POST request and get the response code.
        int httpsResponseCode = https.POST(requestBody);

        if (httpsResponseCode > 0)
        {
            // Serial.printf("[HTTPS] POST... code: %d\n", httpsResponseCode);
        }
        else
        {
            // Serial.printf("[HTTPS] POST... failed, error: %d %s\n", httpsResponseCode, https.errorToString(httpsResponseCode).c_str());
            // Serial.println("(It probably still posted to Slack!)");
        }

        https.end();
        }
        else
        {
        // Serial.print("Unable to connect to ");
        // Serial.println(endpoint);
        }

        delete client;
    }
    else
    {
        // Serial.println("Unable to create client for HTTPS");
    }
}

void sendTwilioNotification(String detectorName, String query, String sid, String key, String number, String endpoint, String label, camera_fb_t *fb) {
    twilio = new Twilio(sid.c_str(), key.c_str());

    char message[150];
    sprintf(message, "Detector (%s) detected %s to the question (%s)!", detectorName.c_str(), label.c_str(), query.c_str());
    String response;
    bool success = twilio->send_message(endpoint, number, message, response);
    if (success) {
        // Serial.println("Sent message successfully!");
    } else {
        // Serial.println(response);
  }
}

Session_Config emailSetup(String host, uint16_t port, String email, String key) {
    /*  Set the network reconnection option */
    MailClient.networkReconnect(true);

    /** Enable the debug via Serial port
     * 0 for no debugging
     * 1 for basic level debugging
     *
     * Debug port can be changed via ESP_MAIL_DEFAULT_DEBUG_PORT in ESP_Mail_FS.h
     */
    smtp.debug(1);

    /* Set the callback function to get the sending results */
    // smtp.callback(smtpCallback);

    /* Declare the Session_Config for user defined session credentials */
    Session_Config config;

    /* Set the session config */
    // config.server.host_name = SMTP_HOST;
    // config.server.port = SMTP_PORT;
    // config.login.email = AUTHOR_EMAIL;
    // config.login.password = AUTHOR_PASSWORD;
    config.server.host_name = host;
    config.server.port = port;
    config.login.email = email;
    config.login.password = key;

    /** Assign your host name or you public IPv4 or IPv6 only
     * as this is the part of EHLO/HELO command to identify the client system
     * to prevent connection rejection.
     * If host name or public IP is not available, ignore this or
     * use generic host "mydomain.net".
     *
     * Assign any text to this option may cause the connection rejection.
     */
    config.login.user_domain = F("mydomain.net");

    /*
    Set the NTP config time
    For times east of the Prime Meridian use 0-12
    For times west of the Prime Meridian add 12 to the offset.
    Ex. American/Denver GMT would be -6. 6 + 12 = 18
    See https://en.wikipedia.org/wiki/Time_zone for a list of the GMT/UTC timezone offsets
    */
    config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
    config.time.gmt_offset = 3;
    config.time.day_light_offset = 0;

    return config;
}

void sendEmailNotification(String detectorName, String query, String key, String email, String endpoint, String host, String label, camera_fb_t *fb) {
    Session_Config config = emailSetup(host, esp_mail_smtp_port_587, email, key);

    /* Declare the message class */
    SMTP_Message message;

    /* Enable the chunked data transfer with pipelining for large message if server supported */
    message.enable.chunking = true;

    /* Set the message headers */
    message.sender.name = F("ESP Mail");
    // message.sender.email = AUTHOR_EMAIL;
    message.sender.email = email;

    // message.subject = F("Test sending camera image");
    String subject = detectorName;
    subject += " detected ";
    subject += label;
    message.subject = subject.c_str();
    // message.addRecipient(F("user1"), RECIPIENT_EMAIL);
    message.addRecipient(F("user1"), endpoint);

    /* Set the message content */
    char message_str[1000];
    sprintf(message_str, "Detector (%s) detected %s to the question (%s)!", detectorName.c_str(), label.c_str(), query.c_str());
    // String content = "<span style=\"color:#ff0000;\">";
    // content += "Result: ";
    // content += label;
    String content = message_str;
    // content += "</span><br/><br/><img src=\"cid:image-001\" alt=\"esp32 cam image\"  width=\"2048\" height=\"1536\">";
    content += "<br/><br/><img src=\"cid:image-001\" alt=\"esp32 cam image\"  width=\"2048\" height=\"1536\">";

    // message.html.content = F("<span style=\"color:#ff0000;\">The camera image.</span><br/><br/><img src=\"cid:image-001\" alt=\"esp32 cam image\"  width=\"2048\" height=\"1536\">");
    message.html.content = content;

    /** The content transfer encoding e.g.
     * enc_7bit or "7bit" (not encoded)
     * enc_qp or "quoted-printable" (encoded) <- not supported for message from blob and file
     * enc_base64 or "base64" (encoded)
     * enc_binary or "binary" (not encoded)
     * enc_8bit or "8bit" (not encoded)
     * The default value is "7bit"
     */
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

    /** The HTML text message character set e.g.
     * us-ascii
     * utf-8
     * utf-7
     * The default value is utf-8
     */
    message.html.charSet = F("utf-8");

    // camera_fb_t *fb = esp_camera_fb_get();

    SMTP_Attachment att;

    /** Set the inline image info e.g.
     * file name, MIME type, file path, file storage type,
     * transfer encoding and content encoding
     */
    att.descr.filename = F("camera.jpg");
    att.descr.mime = F("image/jpg");

    att.blob.data = fb->buf;
    att.blob.size = fb->len;

    att.descr.content_id = F("image-001"); // The content id (cid) of camera.jpg image in the src tag

    /* Need to be base64 transfer encoding for inline image */
    att.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;

    /* Add inline image to the message */
    message.addInlineImage(att);

    /* Connect to the server */
    if (!smtp.connect(&config))
    {
        ESP_MAIL_PRINTF("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
        return;
    }

    // if (smtp.isAuthenticated())
        // Serial.println("\nSuccessfully logged in.");
    // else
        // Serial.println("\nConnected with no Auth.");

    /* Start sending the Email and close the session */
    if (!MailClient.sendMail(&smtp, &message, true))
        ESP_MAIL_PRINTF("Error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());

    // to clear sending result log
    // smtp.sendingResult.clear();

    ESP_MAIL_PRINTF("Free Heap: %d\n", MailClient.getFreeHeap());
}