# ESP32S3 USB Scream Sender

## Overview

The ESP32S3 USB Scream Sender is an innovative project that transforms your ESP32S3 microcontroller into a USB Audio Class 1.0 (UAC 1.0) compliant sound card. This implementation allows you to forward audio from various devices that support USB sound cards (such as PCs, PlayStation 5, Nintendo Switch, and smartphones) to ScreamRouter.

## Key Features

- Implements USB Audio Class 1.0 protocol
- Compatible with a wide range of devices
- Forwards audio to ScreamRouter
- Utilizes the ESP32S3's native USB Device support

## Hardware Requirements

- ESP32S3 microcontroller (required for USB Device support)

## Software Dependencies

- ESP-IDF (Espressif IoT Development Framework) Tested with 5.4 prerelease

## Repository

The source code and additional resources can be found in the following GitHub repository:

[ESP32S3 USB Scream Sender Repository](https://github.com/netham45/esp32s-usb-scream-sender)

## Building and Flashing

To build and flash the project onto your ESP32S3, follow these steps:

1. Clone the repository to your local machine.
2. Navigate to the project directory.
3. Copy `main/secrets_example.h` to `main/secrets.h`.
4. Open `main/secrets.h` and fill out the required information.
5. Connect your ESP32S3 to your computer via USB.
6. Open a terminal in the project directory within the ESP IDF framework.
7. Run the following command, replacing `<esp32s3 com port>` with the appropriate COM port for your device:

```
idf-py -p <esp32s3 com port> flash monitor
```