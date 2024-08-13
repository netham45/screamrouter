# Windows Scream Receiver

There is a Windows Scream Recevier available. It will run the audio through Windows's built-in audio system for any conversions required.

## Download

Download the latest release from the [Windows Scream Receiver](http://github.com/netham45/windows-scream-receiver) repository

## Installation

Windows Scream Receiver comes with installation scripts to install and uninstall it as a system service. These scripts require administrative priveleges in order for the app to set it's priority level to Realtime. To install, run the `install_task.bat` script in the latest release.

During installation you will be asked for a port to listen on. This will be the active port for multicast streams on group 239.255.77.77 and unicast streams on local interfaces on the selected port. The default is 4010.
