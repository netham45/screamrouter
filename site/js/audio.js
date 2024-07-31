let audio = {};
let audio_playing = false;
let visual_playing = false;

function listen_to_sink_button(option) {
    if (audio_playing) {
        stop_audio();
    } else {
        if (option.dataset["is_group"] !== "True") {
            start_audio(option.dataset["ip"]);
        } else {
            alert("Can't listen to group, must listen to sink endpoint");
        }
    }
}

function start_audio(sink_ip) {
    const audiotag = document.getElementById("audio");
    audiotag.pause();
    audiotag.src = `/stream/${sink_ip}/`;
    audiotag.play();
    audiotag.style.display = "inline";
    audio_playing = true;
}

function stop_audio() {
    const audiotag = document.getElementById("audio");
    audiotag.pause();
    audiotag.style.display = "none";
    document.getElementById("mainWrapper").style.display = "none";
    audio_playing = false;
}

window.addEventListener("load", () => {
    const iframe = document.getElementsByTagName("IFRAME")[0];
    const UI = iframe.contentWindow.UI;

    navigator.mediaSession.setActionHandler('play', () => {
        UI.sendKey(0x20, "pause");
    });

    navigator.mediaSession.setActionHandler('pause', () => {
        UI.sendKey(0x20, "pause");
    });
    navigator.mediaSession.setActionHandler('seekbackward', () => {
        console.log('seekbackward');
    });

    navigator.mediaSession.setActionHandler('seekforward', () => {
        console.log('seekforward');
    });

    navigator.mediaSession.setActionHandler('previoustrack', () => {
        UI.sendKey(0xFF51, "left");
    });

    navigator.mediaSession.setActionHandler('nexttrack', () => {
        UI.sendKey(0xFF53, "right");
    });
});
