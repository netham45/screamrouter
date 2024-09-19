# ESP32 Scream Senders for ScreamRouter

ESP32 Scream Senders are tools that allow you to use ESP32 microcontrollers to capture audio from various sources and forward it to ScreamRouter. This document covers two types of senders: USB and Toslink (optical).

## ESP32S3 USB Scream Sender

### Overview

The ESP32S3 USB Scream Sender configures an ESP32S3 microcontroller as a USB Audio Class 1.0 (UAC 1.0) compliant sound card. This implementation allows you to forward audio from various devices that support USB sound cards (such as PCs, PlayStation 5, Nintendo Switch, and smartphones) to ScreamRouter.

### Key Features

- Implements USB Audio Class 1.0 protocol
- Compatible with a wide range of devices
- Forwards audio to ScreamRouter
- Utilizes the ESP32S3's native USB Device support

### Hardware Requirements

- ESP32S3 microcontroller (required for USB Device support)

### Software Dependencies

- ESP-IDF (Espressif IoT Development Framework) Tested with 5.4 prerelease

### Repository

[ESP32S3 USB Scream Sender Repository](https://github.com/netham45/esp32s-usb-scream-sender)

### Building and Flashing

1. Clone the repository to your local machine.
2. Navigate to the project directory.
3. Copy `main/secrets_example.h` to `main/secrets.h`.
4. Open `main/secrets.h` and fill out the required information.
5. Connect your ESP32S3 to your computer via USB.
6. Open a terminal in the project directory within the ESP IDF framework.
7. Run the following command, replacing `<esp32s3 com port>` with the appropriate COM port for your device:

```
idf.py -p <esp32s3 com port> flash monitor
```

## ESP32 Toslink Scream Sender

**Note: This implementation is not tested yet.**

### Overview

The ESP32 Toslink Scream Sender configures an ESP32 microcontroller to receive audio through a Toslink (optical) input and forward it to ScreamRouter. This implementation allows you to capture audio from devices with optical audio output and integrate it into your ScreamRouter setup.

### Key Features

- Receives audio through Toslink (optical) input
- Compatible with devices that have optical audio output
- Forwards audio to ScreamRouter
- Utilizes the ESP32's I2S interface for digital audio reception

### Hardware Requirements

- ESP32 microcontroller
- Toslink (optical) receiver module
- Appropriate power supply for the ESP32 and Toslink module

### Software Dependencies

- ESP-IDF (Espressif IoT Development Framework) Tested with 5.4 prerelease

### Repository

[ESP32 Toslink Scream Sender Repository](https://github.com/netham45/esp32-toslink-sender)

### Building and Flashing

1. Clone the repository to your local machine.
2. Navigate to the project directory.
3. Copy `main/secrets_example.h` to `main/secrets.h`.
4. Open `main/secrets.h` and fill out the required information, including your Wi-Fi credentials and ScreamRouter IP address.
5. Connect your ESP32 to your computer via USB.
6. Open a terminal in the project directory within the ESP IDF framework.
7. Run the following command, replacing `<esp32 com port>` with the appropriate COM port for your device:

```
idf.py -p <esp32 com port> flash monitor
```

## Troubleshooting

If you encounter issues while setting up or using the ESP32 Scream Senders, try the following:

1. Ensure your ESP32 is properly connected and powered.
2. Double-check that you've entered the correct Wi-Fi credentials and ScreamRouter IP in the `secrets.h` file.
3. Verify that your ScreamRouter is running and accessible on the network.
4. For the USB sender, make sure your device recognizes the ESP32S3 as a USB audio device.
5. For the Toslink sender, check that your optical cable is securely connected and not damaged.

If problems persist, please check the issue tracker on the respective GitHub repositories for known issues or to report a new one.

For more information about ScreamRouter and how these senders integrate into the overall system, please refer to the [main README](../README.md) and other documentation files in the Readme directory.