import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
import { highlightActiveSink, highlightActiveSource, highlightActiveRoutes } from "./highlighting.js";
import { getRouteBySinkSource, exposeFunction } from "./utils.js"

export function drawLines() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    const svg = document.getElementById('routeLines') || createSvg();
    const paths = svg.querySelectorAll('path');
    paths.forEach(path => path.remove());
    let enabledLines = [];
    let sourceIdx = 0;
    for (sourceIdx in sources) {
        const source = sources[sourceIdx];
        let sinkIdx = 0;
        for (sinkIdx in sinks) {
            const sink = sinks[sinkIdx];
            const route = getRouteBySinkSource(sink.dataset['name'], source.dataset['name']);
            let enabled = false;
            let drawTheLine = false;
            if (route != null && route.dataset['enabled'].toLowerCase() === "true") {
                drawTheLine = true;
            }
            if (route == null) {
                enabled = false;
                drawTheLine = false;
            }
            if ((editorType == "source" && sink.dataset['routeedit'] === "enabled" &&
                 source.dataset['name'] == selectedSource.dataset['name']) ||
                (editorType == "sink" && source.dataset['routeedit'] === "enabled" &&
                 sink.dataset['name'] == selectedSink.dataset['name'])) {
                enabled = true;
                drawTheLine = true;
            }
            if ((editorType == "source" && sink.dataset['routeedit'] === "disabled" && source == selectedSource) ||
                (editorType == "sink" && source.dataset['routeedit'] === "disabled" && sink == selectedSink)) {
                enabled = false;
                drawTheLine = false;
            }
            if (selectedSink && selectedSink.dataset['name'] == sink.dataset['name'])
                enabled = true;
            if (selectedSource && selectedSource.dataset['name'] == source.dataset['name'])
                enabled = true;
            if (selectedSink && selectedSink.dataset['name'] != sink.dataset['name'])
                enabled = false;
            if (selectedSource && selectedSource.dataset['name'] != source.dataset['name'])
                enabled = false;

                
            if (sink != null && source != null && drawTheLine)
                if (!enabled)
                    drawLine(sink, source, "#AC9", "#F84", "#32C", false);
                else
                    enabledLines.push([sink, source]);
        }
    }
    let idx = 0;
    for (idx in enabledLines)
        drawLine(enabledLines[idx][0], enabledLines[idx][1], "#AC9", "#F84", "#32C", true);
}

function drawLine(sink, source, normalColor, highlightColor, disabledColor, enabled) {
    const svg = document.getElementById('routeLines') || createSvg();
    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    
    const sourceRect = source.getBoundingClientRect();
    const sinkRect = sink.getBoundingClientRect();
    
    const sourceX = sourceRect.right;
    const sourceY = sourceRect.top + sourceRect.height / 2;
    const sinkX = sinkRect.left;
    const sinkY = sinkRect.top + sinkRect.height / 2;
    
    // Calculate control points for the curve
    const midX = (sourceX + sinkX) / 2;
    const controlPoint1X = midX;
    const controlPoint1Y = sourceY;
    const controlPoint2X = midX;
    const controlPoint2Y = sinkY;
    
    // Create the path data for a cubic Bezier curve
    const pathData = `M ${sourceX} ${sourceY} ` +
                     `C ${controlPoint1X} ${controlPoint1Y}, ` +
                     `${controlPoint2X} ${controlPoint2Y}, ` +
                     `${sinkX} ${sinkY}`;
    
    path.setAttribute('d', pathData);
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', enabled?normalColor:disabledColor);
    path.setAttribute('stroke-width', '2');
    path.setAttribute('stroke-linecap', 'round');
    path.setAttribute('class', 'glowline');
    
    // Add a subtle glow effect
    const glow = path.cloneNode(true);
    glow.setAttribute('stroke', 'rgba(255, 255, 255, 0.5)');
    glow.setAttribute('stroke-width', '4');
    glow.setAttribute('filter', 'url(#glow)');
    
    // Create a filter for the glow effect if it doesn't exist
    if (!document.getElementById('glowFilter')) {
        const defs = document.createElementNS('http://www.w3.org/2000/svg', 'defs');
        const filter = document.createElementNS('http://www.w3.org/2000/svg', 'filter');
        filter.setAttribute('id', 'glow');
        
        const feGaussianBlur = document.createElementNS('http://www.w3.org/2000/svg', 'feGaussianBlur');
        feGaussianBlur.setAttribute('stdDeviation', '2');
        feGaussianBlur.setAttribute('result', 'coloredBlur');
        
        const feMerge = document.createElementNS('http://www.w3.org/2000/svg', 'feMerge');
        const feMergeNode1 = document.createElementNS('http://www.w3.org/2000/svg', 'feMergeNode');
        feMergeNode1.setAttribute('in', 'coloredBlur');
        const feMergeNode2 = document.createElementNS('http://www.w3.org/2000/svg', 'feMergeNode');
        feMergeNode2.setAttribute('in', 'SourceGraphic');
        
        feMerge.appendChild(feMergeNode1);
        feMerge.appendChild(feMergeNode2);
        filter.appendChild(feGaussianBlur);
        filter.appendChild(feMerge);
        defs.appendChild(filter);
        svg.appendChild(defs);
    }
    
    // Add hover effect to change stroke color and bring the line to the front
    const changeColor = () => {
        path.setAttribute('stroke', highlightColor);
        glow.setAttribute('stroke', highlightColor);
        
        // Move the hovered path and glow to the end of the SVG, bringing them to the front
        const svg = document.getElementById('routeLines');
        svg.appendChild(glow);
        svg.appendChild(path);
    };
    
    const resetColor = () => {
        path.setAttribute('stroke', enabled?normalColor:disabledColor);
        glow.setAttribute('stroke', 'rgba(255, 255, 255, 0.5)');
        
        // Move the path and glow back to their original position
        const svg = document.getElementById('routeLines');
        if (!enabled) {
            svg.insertBefore(glow, svg.firstChild);
            svg.insertBefore(path, glow.nextSibling);
        } else {
            svg.appendChild(glow);
            svg.appendChild(path);
        }
    };

    const mouseDown = () => {
        if (source.className.indexOf("option-editor") > -1 || 
            sink.className.indexOf("option-editor") > -1)
            return;
        if (selectedSink == sink && selectedSource == source) {
            setSelectedSink("");
            setSelectedSource("");
        } else {
            setSelectedSink(sink);
            setSelectedSource(source);
        }
        highlightActiveSink();
        highlightActiveSource();
        drawLines();
    };

    path.addEventListener('mouseover', changeColor);
    path.addEventListener('mouseout', resetColor);
    glow.addEventListener('mouseover', changeColor);
    glow.addEventListener('mouseout', resetColor);
    glow.addEventListener('mousedown', mouseDown);
    path.addEventListener('mousedown', mouseDown);
    
    svg.appendChild(glow);
    svg.appendChild(path);
}

function createSvg() {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.id = 'routeLines';
    svg.style.position = 'absolute';
    svg.style.top = '0';
    svg.style.left = '0';
    svg.style.width = '100%';
    svg.style.height = '100%';
    document.getElementById("reload").appendChild(svg);
    return svg;
}