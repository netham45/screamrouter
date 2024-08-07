let audio = {};
let audio_playing = false;
let visual_playing = false;
let dummy_audio_running = false;

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
    audiotag.muted = false;
    audiotag.play();
    if (audiotag.prevvolume != null)
        audiotag.volume = audiotag.prevvolume;
    audiotag.style.display = "inline";
    audio_playing = true;
}

function stop_audio() {
    const audiotag = document.getElementById("audio");
    audiotag.pause();
    audiotag.style.display = "none";
    document.getElementById("mainWrapper").style.display = "none";
    audio_playing = false;
    dummy_audio_running = false;
    start_dummy_audio();
}

function start_dummy_audio() {
    if (dummy_audio_running || audio_playing)
        return;
    dummy_audio_running = true;    
    const audiotag = document.getElementById("audio");
    audiotag.pause();
    audiotag.src = '/site/js/tone.wav';
    audiotag.prevvolume = audiotag.volume;
    audiotag.volume = 0;
    audiotag.loop = true;
    audiotag.play();
    audiotag.style.display = "none";
}

window.addEventListener("load", () => {


    navigator.mediaSession.setActionHandler('play', () => {
        console.log("play");
        if (selected_source) {
            let playpause = selected_source.querySelectorAll("SPAN[id^='Play/Pause']");
            if (playpause)
                playpause[0].onclick({target: playpause[0]});
        }
    });

    navigator.mediaSession.setActionHandler('pause', () => {
        console.log("pause");
        if (selected_source) {
            let playpause = selected_source.querySelectorAll("SPAN[id^='Play/Pause']");
            if (playpause)
                playpause[0].onclick({target: playpause[0]});
            }
    });
    navigator.mediaSession.setActionHandler('seekbackward', () => {
        console.log("prevtrack");
        if (selected_source) {
            let prevtrack = selected_source.querySelectorAll("SPAN[id^='Previous Track']");
            if (prevtrack)
                prevtrack[0].onclick({target: prevtrack[0]});
        }
    });

    navigator.mediaSession.setActionHandler('seekforward', () => {
        console.log("nexttrack");
        if (selected_source) {
            let nexttrack = selected_source.querySelectorAll("SPAN[id^='Next Track']");
            if (nexttrack)
                nexttrack[0].onclick({target: nexttrack[0]});
        }
    });

    navigator.mediaSession.setActionHandler('previoustrack', () => {
        console.log("prevtrack");
        if (selected_source) {
            let prevtrack = selected_source.querySelectorAll("SPAN[id^='Previous Track']");
            if (prevtrack)
                prevtrack[0].onclick({target: prevtrack[0]});
        }
    });

    navigator.mediaSession.setActionHandler('nexttrack', () => {
        console.log("nexttrack");
        if (selected_source) {
            let nexttrack = selected_source.querySelectorAll("SPAN[id^='Next Track']");
            if (nexttrack)
                nexttrack[0].onclick({target: nexttrack[0]});
        }
    });
});
