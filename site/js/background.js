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
    const cols = Math.ceil(width / gridSize);
    const rows = Math.ceil(height / gridSize);

    for (let i = 0; i < cols; i++) {
        for (let j = 0; j < rows; j++) {
            if (Math.random() < 0.5) {
                const baseSize = gridSize * (0.8 + Math.random() * 0.4);
                rectangles.push({
                    x: i * gridSize + (Math.random() - 0.5) * gridSize * 0.3,
                    y: j * gridSize + (Math.random() - 0.5) * gridSize * 0.3,
                    width: baseSize * (0.8 + Math.random() * 0.4),
                    height: baseSize * (0.8 + Math.random() * 0.4),
                    rotation: (Math.random() - 0.5) * Math.PI * 0.25,
                    corners: generateCorners(),
                    pulse: Math.random() * Math.PI * 2,
                    pulseSpeed: 0.01 + Math.random() * 0.01,
                    brightness: 0.1 + Math.random() * 0.25 + (1.7 * (Math.random() > .98)),
                    velocityX: (Math.random() - 0.5) * 0.2,
                    velocityY: (Math.random() - 0.5) * 0.2
                });
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

function drawRectangles() {
    backgroundCtx.clearRect(0, 0, width, height);

    for (let i = 0; i < rectangles.length; i++) {
        const rect = rectangles[i];
        rect.pulse += rect.pulseSpeed;
        const scale = 1 + Math.sin(rect.pulse) * 0.1;
        const currentWidth = rect.width * scale;
        const currentHeight = rect.height * scale;

        rect.x += rect.velocityX;
        rect.y += rect.velocityY;

        if (rect.x < -currentWidth) rect.x = width;
        else if (rect.x > width) rect.x = -currentWidth;
        if (rect.y < -currentHeight) rect.y = height;
        else if (rect.y > height) rect.y = -currentHeight;

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