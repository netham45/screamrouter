let already_connected = false;
let visual_already_connected = false;
let canvas_mode = 0;

function start_visualizer(sink_ip) {
    const visualtag = document.getElementById("audio_visualizer");
    visualtag.pause();
    visualtag.src = `/stream/${sink_ip}/`;
    visualtag.play();
    visual_playing = true;

    if (!visual_already_connected) {
        initPlayer();
        const source = audioContext.createMediaElementSource(visualtag);
        visual_already_connected = true;
        source.disconnect(audioContext);
        startRenderer();
        connectToAudioAnalyzer(source);
    }

    document.getElementById("mainWrapper").style.display = "inherit";
}

function stop_visualizer() {
    const visualtag = document.getElementById("audio_visualizer");
    visualtag.pause();
    document.getElementById("mainWrapper").style.display = "none";
    visual_playing = false;
}

function canvas_click() {
    const canvas_holder = document.getElementById("mainWrapper");
    const canvas = document.getElementById("canvas");

    switch (canvas_mode) {
        case 0:
            canvas_holder.style.width = "100%";
            canvas_holder.style.height = "100%";
            canvas.style.width = "100%";
            canvas.style.height = "100%";
            canvas.style.top = 0;
            canvas.style.left = 0;
            canvas_holder.style.position = "absolute";
            canvas.style.position = "absolute";
            document.getElementsByTagName("body")[0].requestFullscreen();
            canvas_holder.style.zIndex = 50;
            break;
        case 1:
            canvas.style.width = "100%";
            canvas.style.height = "100%";
            canvas.style.top = 0;
            canvas.style.left = 0;
            canvas.style.position = "absolute";
            canvas_holder.style.zIndex = -50;
            if (document.fullscreen) document.exitFullscreen();
            break;
        case 2:
            canvas.style.position = "relative";
            canvas_holder.style.position = "relative";
            canvas.top = 300;
            canvas.style.width = "720px";
            canvas.style.height = "480px";
            canvas_holder.style.width = "";
            canvas_holder.style.height = "";
            canvas_holder.style.zIndex = 0;
            break;
    }

    canvas_mode = (canvas_mode + 1) % 3;
}

let visualizer = null;
let rendering = false;
let audioContext = null;
let sourceNode = null;
let delayedAudible = null;
let cycleInterval = null;
let presets = {};
let presetKeys = [];
let presetIndexHist = [];
let presetIndex = 0;
let presetCycle = true;
let presetCycleLength = 15000;
let presetRandom = true;
let canvas = document.getElementById('canvas');

function connectToAudioAnalyzer(sourceNode) {
    if (delayedAudible) delayedAudible.disconnect();
    delayedAudible = audioContext.createDelay();
    delayedAudible.delayTime.value = 0.26;
    sourceNode.connect(delayedAudible);
    visualizer.connectAudio(delayedAudible);
}

function startRenderer() {
    requestAnimationFrame(startRenderer);
    visualizer.render();
}

function nextPreset(blendTime = 5.7) {
    presetIndexHist.push(presetIndex);

    presetIndex = presetRandom
        ? Math.floor(Math.random() * presetKeys.length)
        : (presetIndex + 1) % presetKeys.length;
    visualizer.loadPreset(presets[presetKeys[presetIndex]], blendTime);
    document.getElementById('presetSelect').children[presetIndex].value;
}

function prevPreset(blendTime = 5.7) {
    presetIndex = presetIndexHist.length > 0
        ? presetIndexHist.pop()
        : (presetIndex - 1 + presetKeys.length) % presetKeys.length;
    visualizer.loadPreset(presets[presetKeys[presetIndex]], blendTime);
    document.getElementById('presetSelect').children[presetIndex].value;
}

function restartCycleInterval() {
    if (cycleInterval) clearInterval(cycleInterval);
    if (presetCycle) {
        cycleInterval = setInterval(() => nextPreset(2.7), presetCycleLength);
    }
}

function canvas_onkeydown(e) {
    switch (e.keyCode) {
        case 32:
        case 39:
            nextPreset();
            break;
        case 8:
        case 37:
            prevPreset();
            break;
        case 72:
            nextPreset(0);
            break;
        case 70:
            canvas_click();
            break;
    }
}

function preSelect_change(e) {
    presetIndexHist.push(presetIndex);
    presetIndex = parseInt(e.target.value);
    visualizer.loadPreset(presets[presetKeys[presetIndex]], 5.7);
}

function presetCycle_change(e) {
    presetCycle = e.target.checked;
    restartCycleInterval();
}

function presetCycleLength_change(e) {
    presetCycleLength = parseInt(e.target.value * 1000);
    restartCycleInterval();
}

function presetRandom_change(e) {
    presetRandom = e.target.checked;
}

function initPlayer() {
    audioContext = new AudioContext();

    presets = {};
    if (window.butterchurnPresets) {
        Object.assign(presets, butterchurnPresets.getPresets());
    }
    if (window.butterchurnPresetsExtra) {
        Object.assign(presets, butterchurnPresetsExtra.getPresets());
    }
    presets = _(presets)
        .toPairs()
        .sortBy(([k]) => k.toLowerCase())
        .fromPairs()
        .value();
    presetKeys = _.keys(presets);
    presetIndex = Math.floor(Math.random() * presetKeys.length);

    const presetSelect = document.getElementById('presetSelect');
    for (let i = 0; i < presetKeys.length; i++) {
        const opt = document.createElement('option');
        opt.innerHTML = presetKeys[i].substring(0, 60) + (presetKeys[i].length > 60 ? '...' : '');
        opt.value = i;
        presetSelect.appendChild(opt);
    }

    canvas = document.getElementById('canvas');
    visualizer = butterchurn.default.createVisualizer(audioContext, canvas, {
        width: 3840,
        height: 2160,
        pixelRatio: window.devicePixelRatio || 1,
        textureRatio: 1,
    });

    nextPreset(0);
    cycleInterval = setInterval(() => nextPreset(2.7), presetCycleLength);
}