# ScreamRouter Chrome App Features

This README documents the Chrome app features available in the ScreamRouter application.

## Overview

![ScreamRouter Chrome App](/images/ChromeAppView.png)

ScreamRouter is a Progressive Web App (PWA) that provides audio routing functionality. The Chrome app features enhance the user experience by allowing quick access to key functions through app shortcuts.

## Usage

To use the ScreamRouter Chrome App, you can install it from Chrome's menu. ![Chrome menu install Windows](/images/ChromeAppInstall.png)

It is also available for Chrome on Android. ![Chrome menu install Android](/images/ChromeAndroidInstall.png)

## App Shortcuts

![Chrome shortcuts](/images/ChromeShortcuts.png)

* Shortcuts are only available on Desktop

The following app shortcuts are available:

1. **Play/Pause Source**
   - Short name: PlayPause
   - Description: Play or pause the currently selected audio source
   - URL: #playpause

2. **Next Track**
   - Short name: NextTrack
   - Description: Skip to the next track for the selected source
   - URL: #nexttrack

3. **Previous Track**
   - Short name: PrevTrack
   - Description: Go back to the previous track for the selected source
   - URL: #prevtrack

4. **Enable/Disable Selected Source**
   - Short name: EnableDisableSource
   - Description: Toggle the enabled state of the currently selected audio source
   - URL: #enabledisablesource

5. **Enable/Disable Selected Route**
   - Short name: EnableDisableRoute
   - Description: Toggle the enabled state of the currently selected audio route
   - URL: #enabledisableroute

6. **Enable/Disable Selected Sink**
   - Short name: EnableDisableSink
   - Description: Toggle the enabled state of the currently selected audio sink
   - URL: #enabledisablesink

## Notes

- Make sure the selected source, route, or sink is properly set for the shortcuts to work correctly
- The app must be installed as a PWA for the shortcuts to be available
- Icon files for the shortcuts should be placed in the appropriate directory as specified in the manifest

For more information on how to use these features or contribute to the project, please refer to the main documentation or contact the project maintainers.
