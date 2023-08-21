<p align="center">
  <a href="https://nextjs-fastapi-starter.vercel.app/">
    <img src="https://avatars.githubusercontent.com/u/118213576?s=200&v=4" height="96">
    <h3 align="center">ESP32CAM Edgelight Client</h3>
  </a>
</p>

<br/>

# What is the ESP32CAM Edgelight Client?

The ESP32CAM Edgelight Client is a firmware for the ESP32 microcontroller that allows it to upload images to the Groundlight API.

# How to deploy

## From the Website

This tool is designed to make it as easy as possible to deploy your Groundlight detector on an ESP32 Camera Board. You can deploy your detector in just a few clicks.

1. Go to https://code.groundlight.ai/groundlight-embedded-uploader/.
2. Plug your ESP32 Camera Board into your computer with a USB cable.
3. Click through the steps to upload your detector to your ESP32 Camera Board.

### Enabling Notifications

If you want to receive notifications from your Edgelight deployed detector, you can enable and configure your notification sources from the website.

#### Stacklight Visual Notifications

You can pair your ESP32 Camera Board with a Stacklight to receive visual notifications when your detector is triggered. To do this, you will need to have a Stacklight.

To pair your ESP32 Camera Board with a Stacklight, follow these steps:

1. Go to https://code.groundlight.ai/groundlight-embedded-uploader/.
2. Plug your ESP32 Camera Board into your computer with a USB cable.
3. Flash your ESP32 Camera Board with the Edgelight firmware if you haven't already.
4. On the second step of the deployment process, click the checkbox to enable Stacklight notifications and enter the six digit code on your Stacklight into the text box.
5. Click through the rest of the steps to deploy your detector.

## Building from source

1. Clone this repository
2. Install the [PlatformIO IDE](https://platformio.org/platformio-ide) for VSCode
3. Open the project in VSCode with PlatformIO
4. Plug in your ESP32 with USB to your computer
5. Build and upload the project to your ESP32