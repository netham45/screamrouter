# ScreamRouter Chrome App

## Overview

ScreamRouter, a versatile audio routing and management system, can run as a Progressive Web App (PWA). This feature enhances the user experience by allowing quick access to key functions through app shortcuts, and allowing the application to run as its own app on Android or in its own window on PC.

![ScreamRouter Chrome App](../images/ChromeAppView.png)

For more information about ScreamRouter and its features, please refer to the [main README](../README.md).

## Usage

To use the ScreamRouter Chrome App, you can install it from Chrome's menu. 

![Chrome menu install Windows](../images/ChromeAppInstall.png)

It is also available for Chrome on Android.

<img src="../images/ChromeAndroidInstall.jpg" alt="Chrome menu install Android" width="30%">

## App Shortcuts

![Chrome shortcuts](../images/ChromeShortcuts.png)

* Shortcuts are only available on Desktop

The following app shortcuts are available:

1. **Play/Pause Source**
   - Short name: PlayPause
   - Description: Play or pause the currently selected audio source

2. **Next Track**
   - Short name: NextTrack
   - Description: Skip to the next track for the selected source

3. **Previous Track**
   - Short name: PrevTrack
   - Description: Go back to the previous track for the selected source

4. **Enable/Disable Selected Source**
   - Short name: EnableDisableSource
   - Description: Toggle the enabled state of the currently selected audio source

5. **Enable/Disable Selected Route**
   - Short name: EnableDisableRoute
   - Description: Toggle the enabled state of the currently selected audio route

6. **Enable/Disable Selected Sink**
   - Short name: EnableDisableSink
   - Description: Toggle the enabled state of the currently selected audio sink

## Notes

- Make sure the selected source, route, or sink is properly set for the shortcuts to work correctly
- The app must be installed as a PWA for the shortcuts to be available
- Icon files for the shortcuts should be placed in the appropriate directory as specified in the manifest
