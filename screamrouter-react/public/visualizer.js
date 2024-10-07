// Variable declarations
let visualPlaying = false;
let visualAlreadyConnected = false;
let isFullscreen = false;
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
let canvas = null;

function initVisualizer() {
    canvas = document.getElementById('canvas');
    if (canvas) {
        canvas.addEventListener('click', toggleFullscreen);
        document.addEventListener('keydown', handleKeyDown);
    }
}

function startVisualizer(sinkIp) {
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
}

function stopVisualizer() {
    const visualTag = document.getElementById("audio_visualizer");
    visualTag.pause();
    document.getElementById("mainWrapper").style.display = "none";
    visualPlaying = false;
}

function toggleFullscreen() {
    const canvasHolder = document.getElementById("mainWrapper");
    const presetControls = document.getElementById("presetControls");

    if (!isFullscreen) {
        if (canvasHolder.requestFullscreen) {
            canvasHolder.requestFullscreen();
        }
        canvasHolder.style.position = "fixed";
        canvasHolder.style.top = "0";
        canvasHolder.style.left = "0";
        canvasHolder.style.width = "100%";
        canvasHolder.style.height = "100%";
        canvasHolder.style.zIndex = "9999";
        presetControls.style.display = "none";
    } else {
        if (document.exitFullscreen && document.fullscreenElement) {
                document.exitFullscreen();
        }
        canvasHolder.style.position = "relative";
        canvasHolder.style.width = "100%";
        canvasHolder.style.height = "auto";
        canvasHolder.style.zIndex = "auto";
        presetControls.style.display = "block";
    }
    isFullscreen = !isFullscreen;
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
    document.getElementById('presetSelect').value = presetIndex;
}

function prevPreset(blendTime = 5.7) {
    presetIndex = presetIndexHist.length > 0
        ? presetIndexHist.pop()
        : (presetIndex - 1 + presetKeys.length) % presetKeys.length;
    visualizer.loadPreset(presets[presetKeys[presetIndex]], blendTime);
    document.getElementById('presetSelect').value = presetIndex;
}

function randomPreset(blendTime = 5.7) {
    presetIndexHist.push(presetIndex);
    presetIndex = Math.floor(Math.random() * presetKeys.length);
    visualizer.loadPreset(presets[presetKeys[presetIndex]], blendTime);
    document.getElementById('presetSelect').value = presetIndex;
}

function handleKeyDown(e) {
    switch (e.key.toLowerCase()) {
        case 'f':
            toggleFullscreen();
            break;
        case 'n':
            nextPreset();
            break;
        case 'l':
            prevPreset();
            break;
        case 'h':
            randomPreset();
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

// Expose functions to the global scope
window.startVisualizer = startVisualizer;
window.stopVisualizer = stopVisualizer;
window.toggleFullscreen = toggleFullscreen;

// Initialize the visualizer when the DOM is fully loaded
document.addEventListener('DOMContentLoaded', initVisualizer);