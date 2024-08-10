import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
let backgroundCanvas, backgroundCtx, width, height;
const rectangles = [];

export function onload() {
    backgroundCanvas = document.getElementById('backgroundCanvas');
    backgroundCtx = backgroundCanvas.getContext('2d', { alpha: false });
    onresize();
    animate();
}

export function onresize() {
    width = window.innerWidth;
    height = window.innerHeight;
    backgroundCanvas.width = width;
    backgroundCanvas.height = height;
    initRectangles();
}

function initRectangles() {
    rectangles.length = 0;
    const gridSize = 30;
    const cols = Math.ceil(width / gridSize) + 6; // Add extra columns
    const rows = Math.ceil(height / gridSize) + 6; // Add extra rows
    const buffer = 100; // 100-pixel buffer, matching drawRectangles

    for (let i = -3; i < cols - 3; i++) {
        for (let j = -3; j < rows - 3; j++) {
            if (Math.random() < 0.5) {
                const baseSize = gridSize * (0.8 + Math.random() * 0.4);
                const rect = {
                    width: baseSize * (0.8 + Math.random() * 0.4),
                    height: baseSize * (0.8 + Math.random() * 0.4),
                    rotation: (Math.random() - 0.5) * Math.PI * 0.25,
                    corners: generateCorners(),
                    pulse: Math.random() * Math.PI * 2,
                    pulseSpeed: 0.01 + Math.random() * 0.03,
                    brightness: 0.1 + Math.random() * 0.25 + (1.7 * (Math.random() > .98)),
                    velocityX: (Math.random() - 0.5) * 0.5,
                    velocityY: (Math.random() - 0.5) * 0.5
                };

                rect.x = i * gridSize + (Math.random() - 0.5) * gridSize * 0.3;
                rect.y = j * gridSize + (Math.random() - 0.5) * gridSize * 0.3;

                // Adjust initial position to include buffer zone
                if (rect.x < -buffer) rect.x += width + 2 * buffer;
                else if (rect.x > width + buffer) rect.x -= width + 2 * buffer;
                if (rect.y < -buffer) rect.y += height + 2 * buffer;
                else if (rect.y > height + buffer) rect.y -= height + 2 * buffer;

                rectangles.push(rect);
            }
        }
    }
}

function generateCorners() {
    const corners = [
        { x: -0.5, y: -0.5 },
        { x: 0.5, y: -0.5 },
        { x: 0.5, y: 0.5 },
        { x: -0.5, y: 0.5 }
    ];

    for (let i = 0; i < 1 + Math.floor(Math.random() * 2); i++) {
        const index = 1 + Math.floor(Math.random() * 3);
        const newCorner = {
            x: (corners[index - 1].x + corners[index].x) * 0.5,
            y: (corners[index - 1].y + corners[index].y) * 0.5
        };
        corners.splice(index, 0, newCorner);
    }

    return corners;
}

let lastTime = performance.now();

function drawRectangles() {
    const currentTime = performance.now();
    let deltaTime = (currentTime - lastTime) / 1000; // Convert to seconds
    if (deltaTime > .25) deltaTime = 0; // Any longer than a quarter second is the animation being paused, stop updating to avoid a jumping/skipping effect
    lastTime = currentTime;

    backgroundCtx.clearRect(0, 0, width, height);

    for (let i = 0; i < rectangles.length; i++) {
        const rect = rectangles[i];
        rect.pulse += rect.pulseSpeed * deltaTime * 60; // Adjust pulse speed for time
        const scale = 1 + Math.sin(rect.pulse) * 0.1;
        const currentWidth = rect.width * scale;
        const currentHeight = rect.height * scale;

        // Update position based on velocity and time
        rect.x += rect.velocityX * deltaTime * 60;
        rect.y += rect.velocityY * deltaTime * 60;

        const buffer = 100; // 100-pixel buffer zone around the canvas

        // Check if rectangle has moved off the left edge of the screen (including buffer)
        if (rect.x < -buffer) {
            rect.x = width + buffer; // Move it to the right edge (plus buffer)
        }
        // Check if rectangle has moved off the right edge of the screen (including buffer)
        else if (rect.x > width + buffer) {
            rect.x = -buffer; // Move it to the left edge (minus width and buffer)
        }

        // Check if rectangle has moved off the top edge of the screen (including buffer)
        if (rect.y < -buffer) {
            rect.y = height + buffer; // Move it to the bottom edge (plus buffer)
        }
        // Check if rectangle has moved off the bottom edge of the screen (including buffer)
        else if (rect.y > height + buffer) {
            rect.y = -buffer; // Move it to the top edge (minus height and buffer)
        }
        backgroundCtx.save();
        backgroundCtx.translate(rect.x, rect.y);
        backgroundCtx.rotate(rect.rotation);

        backgroundCtx.beginPath();
        const corners = rect.corners;
        backgroundCtx.moveTo(corners[0].x * currentWidth, corners[0].y * currentHeight);
        for (let j = 1; j < corners.length; j++) {
            backgroundCtx.lineTo(corners[j].x * currentWidth, corners[j].y * currentHeight);
        }
        backgroundCtx.closePath();

        const gradient = backgroundCtx.createRadialGradient(0, 0, 0, 0, 0, Math.max(currentWidth, currentHeight) * 0.5);
        const brightness = rect.brightness * (0.9 + Math.sin(rect.pulse) * 0.1);
        gradient.addColorStop(0, `rgba(0, ${Math.floor(255 * brightness)}, 0, 1)`);
        gradient.addColorStop(1, `rgba(0, ${Math.floor(100 * brightness)}, 0, 0)`);

        backgroundCtx.fillStyle = gradient;
        backgroundCtx.fill();

        backgroundCtx.restore();
    }
}

function animate() {
    drawRectangles();
    requestAnimationFrame(animate);
}