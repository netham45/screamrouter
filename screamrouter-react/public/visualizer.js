// Variable declarations
let visualAlreadyConnected = false;
let isFullscreen = false;
let visualizer = null;
let audioContext = null;
let delayedAudible = null;
let presets = {};
let presetKeys = [];
let presetIndexHist = [];
let presetIndex = 0;
let presetRandom = true;
let canvas = null;

function resizeVisualizer() {
    if (visualizer && canvas) {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
        visualizer.setRendererSize(canvas.width, canvas.height);
    }
}

function initVisualizer() {
    canvas = document.getElementById('canvas');
    if (canvas) {
        canvas.addEventListener('click', toggleFullscreen);
        document.addEventListener('keydown', handleKeyDown);
    }
}

function startVisualizer(sinkIp) {
    const visualTag = document.getElementById("audio_visualizer");
    
    // First pause and clear any existing audio
    visualTag.pause();
    visualTag.removeAttribute('src');
    
    // Wait for the pause to complete
    Promise.resolve().then(() => {
        visualTag.src = sinkIp;
        return visualTag.play().catch(error => {
            if (error.name === 'AbortError') {
                // Ignore abort errors as they're expected during cleanup
                return;
            }
            throw error;
        });
    }).then(() => {
        if (!visualAlreadyConnected) {
            initPlayer();
            const source = audioContext.createMediaElementSource(visualTag);
            visualAlreadyConnected = true;
            source.disconnect(audioContext);
            startRenderer();
            connectToAudioAnalyzer(source);
            // Add resize listener after visualizer is initialized
            window.addEventListener('resize', resizeVisualizer);
            // Initial resize
            resizeVisualizer();
        }
        document.getElementById("mainWrapper").style.display = "inherit";
    }).catch(error => {
        console.error("Error in startVisualizer:", error);
    });
}

function stopVisualizer() {
    const visualTag = document.getElementById("audio_visualizer");
    visualTag.pause();
    visualTag.removeAttribute('src');
    document.getElementById("mainWrapper").style.display = "none";
    window.removeEventListener('resize', resizeVisualizer);
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
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
    visualizer = butterchurn.default.createVisualizer(audioContext, canvas, {
        width: canvas.width,
        height: canvas.height,
        pixelRatio: window.devicePixelRatio || 1,
        textureRatio: 1,
    });

    nextPreset(0);
}

// Expose functions to the global scope
window.startVisualizer = startVisualizer;
window.stopVisualizer = stopVisualizer;
window.toggleFullscreen = toggleFullscreen;

// Initialize the visualizer when the DOM is fully loaded
document.addEventListener('DOMContentLoaded', initVisualizer);
