import {callApi as callApi} from "./api.js"
import { getRouteBySinkSource, exposeFunction } from "./utils.js"
import {nullCallback, restartCallback, editSourceCallback, editSinkCallback, restartCallback2} from "./main.js"

function showDialogCallback(responseText) {
    showShadow();
    document.getElementById("dialog").style.display = "block";
    document.getElementById("dialog").innerHTML = responseText;
    document.getElementById("dialog").scrollIntoView();
}

async function callDialog(url, sourceName = "") {
    let fullUrl = "site/" + url;
    if (sourceName) {
        fullUrl += "/" + sourceName;
    }
    callApi(fullUrl, "GET", "", showDialogCallback);
    if (url === "vnc") {
        await new Promise(r => setTimeout(r, 500));
        if (matchMedia('(pointer:coarse)').matches)
            document.getElementById("vnc-iframe").requestFullscreen();
    }
}

export function dialogAddSource() {
    callDialog("add_source");
}

export function dialogAddSourceGroup() {
    callDialog("add_source_group");
}

export function dialogVnc(sourceName) {
    callDialog("vnc", sourceName);
}

export function dialogUpdateSource(sourceName) {
    callDialog("edit_source", sourceName);
}

export function dialogUpdateSourceEqualizer(sourceName) {
    callDialog("edit_source", sourceName + "/equalizer");
}

export function dialogAddSink() {
    callDialog("add_sink");
}

export function dialogAddSinkGroup() {
    callDialog("add_sink_group");
}

export function dialogUpdateSink(sinkName) {
    callDialog("edit_sink", sinkName);
}

export function dialogUpdateSinkEqualizer(sinkName) {
    callDialog("edit_sink", sinkName + "/equalizer");
}

export function dialogAddRoute() {
    callDialog("add_route");
}

export function dialogUpdateRoute(routeName) {
    callDialog("edit_route", routeName);
}

export function dialogUpdateRouteEqualizer(routeName) {
    callDialog("edit_route", routeName + "/equalizer");
}

export function dialogCancel() {
    document.getElementById("dialog").style.display = "none";
    document.getElementById("dialog").innerHTML = "";
    dismissShadow();
}

export function dialogEqualizerDefault() {
    document.querySelectorAll("DIV#dialog INPUT[TYPE='range']").forEach(input => input.value = 100);
}

function dialogSubmit(close) {
    const result = {};
    let url = "";
    let action = "";
    const equalizer = {};

    document.querySelectorAll("DIV#dialog INPUT, DIV#dialog SELECT").forEach(entry => {
        console.log("Checking entry " + entry.id);
        if (entry.id == undefined) return;
        if (entry.type == "checkbox") {
            result[entry.id] = entry.checked;
        } else if (entry.type == "select-multiple") {
            result[entry.id] = Array.from(entry.selectedOptions).map(option => option.value).filter(value => value != undefined);
        } else if (entry.id == "dialog_url") {
            url = entry.value;
        } else if (entry.id == "dialog_action") {
            action = entry.value;
        } else if (entry.id.startsWith("eqB")) {
            equalizer[entry.name] = parseFloat(entry.value / 100);
        } else {
            const value = entry.value;
            if (parseInt(value).toString() == value) {
                result[entry.id] = parseInt(value);
            } else if (value == "true") {
                result[entry.id] = true;
            } else if (value == "false") {
                result[entry.id] = false;
            } else {
                result[entry.id] = value;
            }
        }
    });
    if (Object.keys(equalizer).length > 0) {
        result["equalizer"] = equalizer;
    }
    console.log("Calling API");
    callApi(url, action, result, close ? restartCallback : undefined);
}

function dialogSubmitClose() {
    dialogSubmit(true);
}

function dialogSubmitNoClose() {
    dialogSubmit(false);
}

export function showShadow() {
    document.getElementById("shadow").style.display = "block";
    document.getElementById("enable_disable").inert = true;
}

export function dismissShadow() {
    document.getElementById("shadow").style.display = "none";
    document.getElementById("enable_disable").inert = false;
}

export function onload() {

    exposeFunction(dialogSubmitClose, "dialogSubmitClose");
    exposeFunction(dialogSubmitNoClose, "dialogSubmitNoClose");
    exposeFunction(dialogSubmit, "dialogSubmit");
    exposeFunction(dialogCancel, "dialogCancel");
}