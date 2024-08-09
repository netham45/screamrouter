import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
import { getRouteBySinkSource, exposeFunction } from "./utils.js"

let audioPlaying = false;
let dummyAudioRunning = false;

function listenToSinkButton(option) {
    if (audioPlaying) {
        stopAudio();
    } else {
        if (option.dataset["is_group"] !== "True") {
            startAudio(option.dataset["ip"]);
        } else {
            alert("Can't listen to group, must listen to sink endpoint");
        }
    }
}

function startAudio(sinkIp) {
    const audioTag = document.getElementById("audio");
    audioTag.pause();
    audioTag.src = `/stream/${sinkIp}/`;
    audioTag.muted = false;
    audioTag.play();
    if (audioTag.prevVolume != null)
        audioTag.volume = audioTag.prevVolume;
    audioTag.style.display = "inline";
    audioPlaying = true;
}

function stopAudio() {
    const audioTag = document.getElementById("audio");
    audioTag.pause();
    audioTag.style.display = "none";
    document.getElementById("mainWrapper").style.display = "none";
    audioPlaying = false;
    dummyAudioRunning = false;
    startDummyAudio();
}

export function startDummyAudio() {
    if (dummyAudioRunning || audioPlaying)
        return;
    if (matchMedia('(pointer:coarse)').matches)
        return;
    dummyAudioRunning = true;    
    const audioTag = document.getElementById("audio");
    audioTag.pause();
    audioTag.src = '/site/js/tone.wav';
    audioTag.prevVolume = audioTag.volume;
    audioTag.volume = 0;
    audioTag.loop = true;
    audioTag.play();
    audioTag.style.display = "none";
}

export function onload() {
    exposeFunction(startDummyAudio, "startDummyAudio");
    exposeFunction(listenToSinkButton, "listenToSinkButton");
    navigator.mediaSession.setActionHandler('play', () => {
        console.log("play");
        if (selectedSource) {
            let playPause = selectedSource.querySelectorAll("BUTTON[id^='Play/Pause']");
            if (playPause)
                playPause[0].onclick({target: playPause[0]});
        }
    });

    navigator.mediaSession.setActionHandler('pause', () => {
        console.log("pause");
        if (selectedSource) {
            let playPause = selectedSource.querySelectorAll("BUTTON[id^='Play/Pause']");
            if (playPause)
                playPause[0].onclick({target: playPause[0]});
            }
    });
    navigator.mediaSession.setActionHandler('seekbackward', () => {
        console.log("prevtrack");
        if (selectedSource) {
            let prevTrack = selectedSource.querySelectorAll("BUTTON[id^='Previous Track']");
            if (prevTrack)
                prevTrack[0].onclick({target: prevTrack[0]});
        }
    });

    navigator.mediaSession.setActionHandler('seekforward', () => {
        console.log("nexttrack");
        if (selectedSource) {
            let nextTrack = selectedSource.querySelectorAll("BUTTON[id^='Next Track']");
            if (nextTrack)
                nextTrack[0].onclick({target: nextTrack[0]});
        }
    });

    navigator.mediaSession.setActionHandler('previoustrack', () => {
        console.log("prevtrack");
        if (selectedSource) {
            let prevTrack = selectedSource.querySelectorAll("BUTTON[id^='Previous Track']");
            if (prevTrack)
                prevTrack[0].onclick({target: prevTrack[0]});
        }
    });

    navigator.mediaSession.setActionHandler('nexttrack', () => {
        console.log("nexttrack");
        if (selectedSource) {
            let nextTrack = selectedSource.querySelectorAll("BUTTON[id^='Next Track']");
            if (nextTrack)
                nextTrack[0].onclick({target: nextTrack[0]});
        }
    });
}
