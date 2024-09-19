# Setting up ScreamSender on Raspberry Pi Zero

This guide provides instructions for setting up ScreamSender on a Raspberry Pi Zero to forward USB audio to ScreamRouter. This setup allows you to use a Raspberry Pi Zero as a USB audio device that can send audio to ScreamRouter over the network.

## Prerequisites

- Raspberry Pi Zero 2 W or Raspberry Pi Zero W
- Raspberry Pi OS Lite (32-bit) installed and configured
- Access to the Raspberry Pi via SSH or terminal

## Setup Instructions

### 1. Configure USB Gadget

Add the following lines to `/boot/config.txt`:

```
dtoverlay=dwc2
```

Add the following lines to `/etc/modules`:

```
dwc2
libcomposite
```

### 2. Create USB Gadget Script

Create a new file `/usr/bin/usb_gadget_audio.sh` with the following content:

```bash
#!/bin/bash

IP_ADDRESS=192.168.3.114
PORT=16401
SAMPLE_RATE=48000
BIT_DEPTH=16
CHANNELS=2 # screamsender only supports two currently

cd /sys/kernel/config/usb_gadget/
mkdir -p audio_gadget
cd audio_gadget

echo 0x1d6b > idVendor # Linux Foundation
echo 0x0104 > idProduct # Multifunction Composite Gadget
echo 0x0100 > bcdDevice # v1.0.0
echo 0x0200 > bcdUSB # USB2

mkdir -p strings/0x409
echo "fedcba9876543210" > strings/0x409/serialnumber
echo "Scream Sender" > strings/0x409/manufacturer
echo "Scream Sender" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "Audio Config" > configs/c.1/strings/0x409/configuration
echo 250 > configs/c.1/MaxPower

# Audio function
mkdir -p functions/uac1.usb0
ln -s functions/uac1.usb0 configs/c.1/

# Set audio function parameters
echo $SAMPLE_RATE > functions/uac1.usb0/p_sampling_freq
echo $SAMPLE_RATE > functions/uac1.usb0/c_sampling_freq
echo $CHANNELS > functions/uac1.usb0/p_chmask
echo $CHANNELS > functions/uac1.usb0/c_chmask
echo $BIT_DEPTH > functions/uac1.usb0/p_ssize
echo $BIT_DEPTH > functions/uac1.usb0/c_ssize

# Enable gadget
ls /sys/class/udc > UDC

sleep 2
nohup bash -c "while x=x; do arecord -D hw:CARD=UAC1Gadget,DEV=0 -f dat 2>/var/log/arecord | screamsender -i $IP_ADDRESS -p $PORT -s $SAMPLE_RATE -b $BIT_DEPTH &> /var/log/arecord;done" &
```

Make the script executable:

```
sudo chmod +x /usr/bin/usb_gadget_audio.sh
```

### 3. Configure Auto-start

Add the following line to `/etc/rc.local` before the `exit 0` line:

```
/usr/bin/usb_gadget_audio.sh
```

### 4. Install Dependencies

```
sudo apt-get update
sudo apt-get install -y g++ git
```

### 5. Download and Build ScreamSender

```
git clone https://github.com/netham45/screamsender.git
cd screamsender
./build.sh
sudo cp screamsender /usr/bin/screamsender
```

### 6. Reboot

```
sudo reboot
```

After rebooting, the Raspberry Pi should be configured as a USB audio device and start forwarding audio using ScreamSender.

## Integration with ScreamRouter

To use this ScreamSender with ScreamRouter:

1. In ScreamRouter, add a new source with the IP address of your Raspberry Pi Zero.
2. Set the port to match the PORT value in the `usb_gadget_audio.sh` script (default is 16401).
3. Configure the audio format settings (bit depth, sample rate, channels) to match the values in the script.

ScreamRouter will then be able to receive audio from your Raspberry Pi Zero USB audio device.

## Troubleshooting

- If the USB audio device is not recognized, check the USB gadget configuration in `/boot/config.txt` and `/etc/modules`.
- If audio is not being sent to ScreamRouter, verify the IP address and port in the `usb_gadget_audio.sh` script.
- Ensure that your network allows UDP traffic on the specified port between the Raspberry Pi Zero and ScreamRouter.

## Note

Ensure to replace the IP_ADDRESS and PORT variables in the `usb_gadget_audio.sh` script with the appropriate values for your ScreamRouter setup.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.