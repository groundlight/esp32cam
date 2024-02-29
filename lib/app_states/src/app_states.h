
#pragma once

#include <Arduino.h>

enum QueryState {
  WAITING_TO_QUERY,
  DNS_NOT_FOUND,
  SSL_CONNECTION_FAILURE,
  LAST_RESPONSE_PASS,
  LAST_RESPONSE_FAIL,
  LAST_RESPONSE_UNSURE,
  NOT_AUTHENTICATED,
};

enum NotificationState {
  NOTIFICATION_NOT_ATTEMPTED,
  NOTIFICATIONS_SENT,
  NOTIFICATION_ATTEMPT_FAILED,
};

String queryStateToString (QueryState state) {
  switch (state) {
    case WAITING_TO_QUERY:
      return "WAITING_TO_QUERY";
    case SSL_CONNECTION_FAILURE:
      return "CONNECTION_FAILURE";
    case DNS_NOT_FOUND:
      return "DNS_NOT_FOUND";
    case LAST_RESPONSE_PASS:
      return "LAST_RESPONSE_PASS";
    case LAST_RESPONSE_FAIL:
      return "LAST_RESPONSE_FAIL";
    case LAST_RESPONSE_UNSURE:
      return "LAST_RESPONSE_UNSURE";
    case NOT_AUTHENTICATED:
      return "NOT_AUTHENTICATED";
    default:
      return "UNKNOWN";
  }
}

String notificationStateToString (NotificationState state) {
  switch (state) {
    case NOTIFICATION_NOT_ATTEMPTED:
      return "NOTIFICATION_NOT_ATTEMPTED";
    case NOTIFICATIONS_SENT:
      return "NOTIFICATIONS_SENT";
    case NOTIFICATION_ATTEMPT_FAILED:
      return "NOTIFICATION_ATTEMPT_FAILED";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief A class to lock the ESP32 frame buffer
 *
 * This class is used to lock the ESP32 frame buffer. It is used mainly for RAII, so that the frame buffer is properly released when the object goes out of scope.
 * It's not a true lock, the pointer to the frame is freely accessbile
 *
 * @todo Implement this class
 */
class EspFrameLock {
  public:
    camera_fb_t* frame = NULL;

    EspFrameLock() {
      frame = esp_camera_fb_get();
    }

    ~EspFrameLock() {
      esp_camera_fb_return(frame);
      delete frame;
    }
     EspFrameLock& operator=(const EspFrameLock&) = delete; // disallow assignment
     EspFrameLock(const EspFrameLock&) = delete; // disallow copying
};