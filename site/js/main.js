import {highlightActiveSink, highlightActiveSource, highlightActiveRoutes, onload as highlightingOnload} from "./highlighting.js";
import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
import {onload as audioOnload} from "./audio.js";
import {onload as sortOnload} from "./sort.js";
import {onload as iframeOnload} from "./iframe.js";
import {onload as dragOnload} from "./drag.js";
import {onload as dialogOnload, dismissShadow, showShadow} from "./dialog.js";
import {onload as controlsOnload} from "./controls.js";
import {onload as routeSelectorOnload} from "./route_selector.js";
import {onload as visualizerOnload} from "./visualizer.js";
import {onload as backgroundOnload, onresize as backgroundOnresize} from "./background.js";
import {drawLines} from "./lines.js";
import {callApi as callApi} from "./api.js"

export function nullCallback(responseText) {
}

export function restartCallback(responseText) {
    callApi("/body", "get", {}, restartCallback2);
}

function updateSelectionAndHighlight(responseText, selectedSinkName = "", selectedSourceName = "") {
    let reloadDiv = document.getElementById("reload");
    reloadDiv.innerHTML = "";
    reloadDiv.innerHTML = responseText;
    let selectedSinkQuery = document.querySelectorAll("DIV#select-sinks SPAN[DATA-NAME='" + selectedSinkName + "']");
        if (selectedSinkQuery.length > 0)
            setSelectedSink(selectedSinkQuery[0]);
        else
        setSelectedSink("");
    let selectedSourceQuery = document.querySelectorAll("DIV#select-sources SPAN[DATA-NAME='" + selectedSourceName + "']");
    if (selectedSourceQuery.length  >  0)
        setSelectedSource(selectedSourceQuery[0]);
    else
        setSelectedSource("");

    highlightActiveSink();
    highlightActiveSource();

    drawLines();
}

export function editSourceCallback(responseText) {
    let selectedSinkName = selectedSink ? selectedSink.dataset['name'] : "";
    updateSelectionAndHighlight(responseText, selectedSinkName, "");
    if (selectedSource) highlightActiveSource(false);
}

export function editSinkCallback(responseText) {
    let selectedSourceName = selectedSource ? selectedSource.dataset['name'] : "";
    updateSelectionAndHighlight(responseText, "", selectedSourceName);
    if (selectedSink) highlightActiveSink(false);
}

export function restartCallback2(responseText) {
    let selectedSinkName = "";
    if (selectedSink)
        selectedSinkName = selectedSink.dataset['name'];
    let selectedSourceName = "";
    if (selectedSource)
        selectedSourceName = selectedSource.dataset['name'];
    let selectedRouteName = "";
    if (selectedRoute)
        selectedRouteName = selectedRoute.dataset['name'];
    let selectedElementId = document.activeElement.id;
    let reloadDiv = document.getElementById("reload");
    const scrollX = window.scrollX;
    const scrollY = window.scrollY;
    reloadDiv.innerHTML = "";
    reloadDiv.innerHTML = responseText;
    window.scrollTo(scrollX, scrollY);
    let selectedSinkQuery = document.querySelectorAll("DIV#select-sinks SPAN[DATA-NAME='" + selectedSinkName + "']");
    if (selectedSinkQuery.length > 0)
        setSelectedSink(selectedSinkQuery[0]);
    else
        setSelectedSink("");
    let selectedSourceQuery = document.querySelectorAll("DIV#select-sources SPAN[DATA-NAME='" + selectedSourceName + "']");
    if (selectedSourceQuery.length  >  0)
        setSelectedSource(selectedSourceQuery[0]);
    else
        setSelectedSource("");
    let selectedRouteQuery = document.querySelectorAll("DIV#select-routes SPAN[DATA-NAME='"  + selectedRouteName  + "']");
    if (selectedRouteQuery.length > 0)
        setSelectedRoute(selectedRouteQuery[0]);
    else
        setSelectedRoute("");
    if (selectedSink) highlightActiveSink();
    if (selectedSource) highlightActiveSource();
    highlightActiveRoutes();
    let selectedElementQuery = document.getElementById(selectedElementId);
    if (selectedElementQuery)
        selectedElementQuery.focus();
    dialogCancel();
    dismissShadow();
    drawLines();
}

function startRefreshing() {
    let lastTouchTime = Date.now();
    function updateTouch() {
        lastTouchTime = Date.now();
    }
    document.addEventListener('touchstart', updateTouch);
    document.addEventListener('mousemove', updateTouch);
    document.addEventListener('keydown', updateTouch);

    setInterval(
        function() {
            if (window['stop_reload'])
                return;
            const idleTime = Date.now() - lastTouchTime;
            if (idleTime < 15 * 60 * 1000)
                callApi("/body", "get", {}, restartCallback2);
        }, 5000);
}

function onload() {
    dragOnload();
    audioOnload();
    sortOnload();
    dialogOnload();
    backgroundOnload();
    routeSelectorOnload();
    highlightingOnload();
    controlsOnload();
    iframeOnload();
    visualizerOnload();
    drawLines();
    startRefreshing();
}

function onresize() {
    drawLines();
    backgroundOnresize();
}

window.addEventListener('load', onload);
window.addEventListener('resize', onresize);
