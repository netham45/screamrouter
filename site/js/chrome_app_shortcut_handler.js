import { selectedSource, selectedRoute, selectedSink } from "./global.js";


export function onload() {
    if ('launchQueue' in window) {
        window.launchQueue.setConsumer(launchParams => {
            switch (launchParams.targetURL.split("#")[1]) {
                case "enabledisablesink":
                    if (selectedSink) {
                        let enabledisable = selectedSink.querySelectorAll("BUTTON.button-option-enable");
                        if (enabledisable)
                            enabledisable[0].onclick({target: enabledisable[0]});
                    }
                    break;
                case "enabledisableroute":
                    if (selectedRoute) {
                        let enabledisable = selectedRoute.querySelectorAll("BUTTON.button-option-enable");
                        if (enabledisable)
                            enabledisable[0].onclick({target: enabledisable[0]});
                    }
                    break;
                case "enabledisablesource":
                    if (selectedSource) {
                        let enabledisable = selectedSource.querySelectorAll("BUTTON.button-option-enable");
                        if (enabledisable)
                            enabledisable[0].onclick({target: enabledisable[0]});
                    }
                    break;
                case "playpause":
                    if (selectedSource) {
                        let playPause = selectedSource.querySelectorAll("BUTTON[id^='Play/Pause']");
                        if (playPause)
                            playPause[0].onclick({target: playPause[0]});
                    }
                    break;
                case "prevtrack":
                    if (selectedSource) {
                        let prevTrack = selectedSource.querySelectorAll("BUTTON[id^='Previous Track']");
                        if (prevTrack)
                            prevTrack[0].onclick({target: prevTrack[0]});
                    }
                    break;
                case "nexttrack":
                    if (selectedSource) {
                        let nextTrack = selectedSource.querySelectorAll("BUTTON[id^='Next Track']");
                        if (nextTrack)
                            nextTrack[0].onclick({target: nextTrack[0]});
                    }
                    break;
                default:
                    console.log("Unknown shortcut URL " + launchParams.targetURL);
                    break;
            }
        });
    }
}
