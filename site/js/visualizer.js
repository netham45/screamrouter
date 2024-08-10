import { exposeFunction } from "./utils.js";

// Variable declarations
export let visualPlaying = false;
let visualAlreadyConnected = false;
let canvasMode = 1;
let visualizer = null;
let audioContext = null;
let delayedAudible = null;
let presets = {};
let presetKeys = [];
let presetIndexHist = [];
let presetIndex = 0;
let cycleInterval = 0;
let presetCycleLength = 15000;
let presetRandom = true;
let canvas = document.getElementById('canvas');

export function startVisualizer(sinkIp) {
    const visualTag = document.getElementById("audio_visualizer");
    visualTag.pause();
    visualTag.src = `/stream/${sinkIp}/`;
    visualTag.play();
    visualPlaying = true;

    if (!visualAlreadyConnected) {
        initPlayer();
        const source = audioContext.createMediaElementSource(visualTag);
        visualAlreadyConnected = true;
        source.disconnect(audioContext);
        startRenderer();
        connectToAudioAnalyzer(source);
    }

    document.getElementById("mainWrapper").style.display = "inherit";
    canvasMode = 1;
    canvasClick();
}

export function stopVisualizer() {
    const visualTag = document.getElementById("audio_visualizer");
    visualTag.pause();
    document.getElementById("mainWrapper").style.display = "none";
    visualPlaying = false;
}

function canvasClick() {
    const canvasHolder = document.getElementById("mainWrapper");
    const canvas = document.getElementById("canvas");

    switch (canvasMode) {
        case 0:
            canvasHolder.style.width = "100%";
            canvasHolder.style.height = "100%";
            canvas.style.width = "100%";
            canvas.style.height = "100%";
            canvas.style.top = 0;
            canvas.style.left = 0;
            canvasHolder.style.position = "absolute";
            canvas.style.position = "absolute";
            document.getElementsByTagName("body")[0].requestFullscreen();
            canvasHolder.style.zIndex = 50;
            break;
        case 1:
            canvasHolder.style.width = "100%";
            canvasHolder.style.height = "100%";
            canvas.style.width = "100%";
            canvas.style.height = "100%";
            canvas.style.top = 0;
            canvas.style.left = 0;
            canvasHolder.style.position = "absolute";
            canvas.style.position = "absolute";
            canvasHolder.style.zIndex = -50;
            if (document.fullscreen) document.exitFullscreen();
            break;
        case 2:
            canvas.style.position = "relative";
            canvasHolder.style.position = "relative";
            canvas.top = 300;
            canvas.style.width = "720px";
            canvas.style.height = "480px";
            canvasHolder.style.width = "";
            canvasHolder.style.height = "";
            canvasHolder.style.zIndex = 0;
            break;
}

    canvasMode = (canvasMode + 1) % 2;
}

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

export function canvasOnKeyDown(e) {
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
            canvasClick();
            break;
    }
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

export function onload() {
    exposeFunction(canvasClick, "canvasClick");
    exposeFunction(canvasOnKeyDown, "canvasOnKeyDown");
}