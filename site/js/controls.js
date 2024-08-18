import {callApi as callApi} from "./api.js"
import {nullCallback, restartCallback, editSourceCallback, editSinkCallback, restartCallback2} from "./main.js"
import { getRouteBySinkSource, exposeFunction } from "./utils.js"
import { visualPlaying, startVisualizer, stopVisualizer } from "./visualizer.js"
import { 
    dialogAddSource, 
    dialogAddSourceGroup, 
    dialogAddSink, 
    dialogAddSinkGroup, 
    dialogAddRoute, 
    dialogVnc, 
    dialogUpdateSourceEqualizer, 
    dialogUpdateSinkEqualizer, 
    dialogUpdateRouteEqualizer, 
    dialogUpdateSource, 
    dialogUpdateSink, 
    dialogUpdateRoute 
} from "./dialog.js"

function addSourceButton() {
    dialogAddSource();
}

function addSourceGroupButton() {
    dialogAddSourceGroup();
}

function addSinkButton() {
    dialogAddSink();
}

function addSinkGroupButton() {
    dialogAddSinkGroup();
}

function addRouteButton() {
    dialogAddRoute();
}

function volumeSliderChange(e) {
    const volumeElement = e.target;
    const optionElement = e.target.parentNode.parentNode;
    const type = optionElement.dataset["type"].replace("Description","").toLowerCase();
    const volumeLevel = volumeElement.value / 100;
    callApi(`/${type}s/${optionElement.dataset["name"]}/volume/${volumeLevel}`, "get");
}

function timeshiftLiveOnclick(e) {
    const optionElement = e.target.parentNode.parentNode;
    const type = optionElement.dataset["type"].replace("Description","").toLowerCase();
    const timeshift = 0;
    optionElement.dataset['timeshift'] = timeshift.toString();
    let label = e.target.parentNode.querySelector('[FOR$="timeshift"]');
    label.innerHTML = "Timeshift: -" + timeshift + " seconds";
    callApi(`/${type}s/${optionElement.dataset["name"]}/timeshift/${timeshift}`, "get");
}

function timeshiftFastForwardOnclick(e) {
    const optionElement = e.target.parentNode.parentNode;
    const type = optionElement.dataset["type"].replace("Description","").toLowerCase();
    console.log(optionElement);
    let timeshift = parseFloat(optionElement.dataset['timeshift']) - 5;
    if (timeshift < 0)
        timeshift = 0;
    optionElement.dataset['timeshift'] = timeshift.toString();
    console.log('label[FOR="' + e.target.id + '"]');
    let label = e.target.parentNode.querySelector('[FOR$="timeshift"]');
    label.innerHTML = "Timeshift: -" + timeshift + " seconds";
    callApi(`/${type}s/${optionElement.dataset["name"]}/timeshift/${timeshift}`, "get");
}

function timeshiftRewindOnclick(e) {
    const optionElement = e.target.parentNode.parentNode;
    const type = optionElement.dataset["type"].replace("Description","").toLowerCase();
    const timeshift = parseFloat(optionElement.dataset['timeshift']) + 5;
    optionElement.dataset['timeshift'] = timeshift.toString();
    let label = e.target.parentNode.querySelector('[FOR$="timeshift"]');
    label.innerHTML = "Timeshift: -" + timeshift + " seconds";
    callApi(`/${type}s/${optionElement.dataset["name"]}/timeshift/${timeshift}`, "get");
}

function visualizerIconOnclick(e) {
    const option = e.target.parentNode.parentNode;
    if (visualPlaying) {
        stopVisualizer();
    } else {
        if (option.dataset["isGroup"] !== "True") {
            startVisualizer(option.dataset["ip"]);
        } else {
            alert("Can't listen to group, must listen to sink endpoint");
        }
    }
}

function enableDisableButton(event) {
    let option = event.target.parentNode.parentNode;
    const type = option.dataset["type"].replace("Description","").toLowerCase();
    callApi("/" + type + "s/" + option.dataset["name"] + (option.dataset["enabled"] == "True"? "/disable" : "/enable"), 'get', {}, restartCallback);
}

function enableDisableSourceButton() {
    const selectedSources = getSelectedSources();
    if (selectedSources.length !== 1) {
        alert("Exactly one source must be selected to edit");
        return;
    }
    const enableDisable = selectedSources[0].classList.contains("enabled") ? "/disable" : "/enable";
    callApi(`/sources/${selectedSources[0].dataset["name"]}${enableDisable}`, "get", 0, restartCallback);
}

function vncIconOnclick(e) {
    dialogVnc(e.target.parentNode.parentNode.dataset["name"]);
}

function playPauseIconOnclick(e) {
    callApi(`/sources/${e.target.parentNode.parentNode.dataset["name"]}/play`, "get", 0, nullCallback);
}

function nextTrackIconOnclick(e) {
    callApi(`/sources/${e.target.parentNode.parentNode.dataset["name"]}/nexttrack`, "get", 0, nullCallback);
}

function previousTrackIconOnclick(e) {
    callApi(`/sources/${e.target.parentNode.parentNode.dataset["name"]}/prevtrack`, "get", 0, nullCallback);
}

function equalizerIconOnclick(e) {
    const parentNode = e.target.parentNode.parentNode;
    const type = parentNode.dataset["type"];
    const name = parentNode.dataset["name"];
    if (type === "SourceDescription") {
        dialogUpdateSourceEqualizer(name);
    } else if (type === "SinkDescription") {
        dialogUpdateSinkEqualizer(name);
    } else if (type === "RouteDescription") {
        dialogUpdateRouteEqualizer(name);
    }
}

function updateIconOnclick(e) {
    const parentNode = e.target.parentNode.parentNode;
    const type = parentNode.dataset["type"];
    const name = parentNode.dataset["name"];
    if (type === "SourceDescription") {
        dialogUpdateSource(name);
    } else if (type === "SinkDescription") {
        dialogUpdateSink(name);
    } else if (type === "RouteDescription") {
        dialogUpdateRoute(name);
    }
}

function volumeIconOnclick(e) {
    const parentNode = e.target.parentNode.parentNode;
    const type = parentNode.dataset["type"];
    let endpoint = "";
    let enableDisable = "";
    if (type === "SourceDescription" || type === "SinkDescription") {
        endpoint = type === "SourceDescription" ? "/sources/" : "/sinks/";
        enableDisable = parentNode.dataset["enabled"] === "True" ? "/disable" : "/enable";
    } else if (type === "RouteDescription") {
        endpoint = "/routes/";
        enableDisable = parentNode.dataset["enabled"] === "True" ? "/disable" : "/enable";
    }
    callApi(`${endpoint}${parentNode.dataset["name"]}${enableDisable}`, "get", 0, restartCallback);
}

function removeIconOnclick(e) {
    const parentNode = e.target.parentNode.parentNode;
    const type = parentNode.dataset["type"];
    let endpoint = "";
    let confirmType = "";
    if (type === "SourceDescription") {
        endpoint = "/sources/";
        confirmType = "source";
    } else if (type === "SinkDescription") {
        endpoint = "/sinks/";
        confirmType = "sink";
    } else if (type === "RouteDescription") {
        endpoint = "/routes/";
        confirmType = "route";
    }
    const doIt = confirm(`Are you sure you want to remove the ${confirmType} '${parentNode.dataset["name"]}'?`);
    if (doIt) {
        callApi(`${endpoint}${parentNode.dataset["name"]}`, "delete", 0, restartCallback);
    }
}

function listenIconOnclick(e) {
    listenToSinkButton(e.target.parentNode.parentNode);
}

function keypressHandler(event) {
    if (event.key === "Enter" || event.key === "Space") {
        if (event.target.onclick)
            event.target.onclick(event);
    }
}

export function onload() {
    exposeFunction(addSourceButton, "addSourceButton");
    exposeFunction(addSourceGroupButton, "addSourceGroupButton");
    exposeFunction(addSinkButton, "addSinkButton");
    exposeFunction(addSinkGroupButton, "addSinkGroupButton");
    exposeFunction(addRouteButton, "addRouteButton");
    exposeFunction(volumeSliderChange, "volumeSliderChange");
    exposeFunction(timeshiftLiveOnclick, "timeshiftLiveOnclick");
    exposeFunction(timeshiftRewindOnclick, "timeshiftRewindOnclick");
    exposeFunction(timeshiftFastForwardOnclick, "timeshiftFastForwardOnclick");
    exposeFunction(visualizerIconOnclick, "visualizerIconOnclick");
    exposeFunction(enableDisableButton, "enableDisableButton");
    exposeFunction(enableDisableSourceButton, "enableDisableSourceButton");
    exposeFunction(vncIconOnclick, "vncIconOnclick");
    exposeFunction(playPauseIconOnclick, "playPauseIconOnclick");
    exposeFunction(nextTrackIconOnclick, "nextTrackIconOnclick");
    exposeFunction(previousTrackIconOnclick, "previousTrackIconOnclick");
    exposeFunction(equalizerIconOnclick, "equalizerIconOnclick");
    exposeFunction(updateIconOnclick, "updateIconOnclick");
    exposeFunction(volumeIconOnclick, "volumeIconOnclick");
    exposeFunction(removeIconOnclick, "removeIconOnclick");
    exposeFunction(listenIconOnclick, "listenIconOnclick");
    exposeFunction(keypressHandler, "keypressHandler");
}