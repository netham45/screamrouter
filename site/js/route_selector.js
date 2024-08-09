import {callApi as callApi} from "./api.js"
import {drawLines as drawLines} from "./lines.js"
import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
import { getRouteBySinkSource, exposeFunction } from "./utils.js"
import {nullCallback, restartCallback, editSourceCallback, editSinkCallback, restartCallback2} from "./main.js"

function getRouteByName(name) {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    for (let routeIdx in routes)
        if (routes[routeIdx].dataset['name'] == name)
            return routes[routeIdx];
    return null;
}

function createRoute(name, sink, source, volume=null, equalizer=null, delay=null, enabled=null) {
    if (volume == null) volume = 1.0;
    if (equalizer == null) equalizer = {
                                        "b1": 1.0,
                                        "b2": 1.0,
                                        "b3": 1.0,
                                        "b4": 1.0,
                                        "b5": 1.0,
                                        "b6": 1.0,
                                        "b7": 1.0,
                                        "b8": 1.0,
                                        "b9": 1.0,
                                        "b10": 1.0,
                                        "b1": 1.0,
                                        "b12": 1.0,
                                        "b13": 1.0,
                                        "b14": 1.0,
                                        "b15": 1.0,
                                        "b16": 1.0,
                                        "b17": 1.0,
                                        "b18": 1.0,};
    if (delay == null) delay = 0;
    if (enabled == null) enabled = true;
    let route = {"name": name,
                 "sink": sink,
                 "source": source,
                 "volume": volume,
                 "equalizer": equalizer,
                 "delay": delay,
                 "enabled": enabled}
    let url = "routes";
    let method = "post";
    callApi(url, method, route, restartCallback);
}

function updateRoute(name, sink=null, source=null, volume=null, equalizer=null, delay=null, enabled=null) {
        let routeElement = getRouteByName(name);
        if (sink == null) sink = routeElement.dataset['sink'];
        if (source == null) source = routeElement.dataset['source'];
        if (volume == null) volume = routeElement.dataset['volume'];
        if (equalizer == null) { 
            let equalizerJson = "{" + routeElement.dataset['equalizer'].replace(/b(\d+)=(\d+\.?\d*)/g, '"b$1": $2,') + "}";
            equalizerJson = equalizerJson.replace(",}", "}");
            equalizer = JSON.parse(equalizerJson);
        }
        if (delay ==  null) delay = routeElement.dataset['delay'];
        if (enabled ==  null) enabled = routeElement.dataset['enabled'].toLowerCase() == "true";
        let route = {"name": name,
                     "sink": sink,
                     "source": source,
                     "volume": volume,
                     "equalizer": equalizer,
                     "delay": delay,
                     "enabled": enabled}
        let url ="routes/" + name;
        let method = "put";
        callApi(url, method, route, restartCallback);
}

function editSinkSources(e) {
    setEditorActive(true);
    let tgt = e.target.parentNode.parentNode;
    const type = tgt.dataset["type"].replace("Description","").toLowerCase();
    setEditorType(type);
    if (type === "source")
        highlightSinksBasedOffSource(tgt);
    if (type === "sink")
        highlightSourcesBasedOffSink(tgt);
}

function highlightSinksBasedOffSource(source) {
    callApi("site/edit_source_routes/" + source.dataset['name'], "get", {}, editSinkCallback);
}

function highlightSourcesBasedOffSink(sink) {
    callApi("site/edit_sink_routes/" + sink.dataset['name'], "get", {}, editSourceCallback);
}

function saveSourceRoutes(source) {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    for (let sinkIdx in sinks) {
        const sink = sinks[sinkIdx];
        const enabled = sink.className.indexOf("Enable") > -1;
        let route = getRouteBySinkSource(sink.dataset['name'], source.dataset['name']);
        if (route == null && enabled) {
            createRoute(source.dataset['name'] + " To " + sink.dataset['name'], sink.dataset['name'], source.dataset['name']);
        } else {
            updateRoute(route.dataset['name'], null, null, null, null, null, enabled);
        }
    }
}

function saveSinkRoutes(sink) {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    for (let sourceIdx in sources) {
        const source = sources[sourceIdx];
        const enabled = source.className.indexOf("Enable") > -1;
        let route = getRouteBySinkSource(sink.dataset['name'], source.dataset['name']);
        if (route == null) {
            if (enabled)
                createRoute(source.dataset['name'] + " To " + sink.dataset['name'], sink.dataset['name'], source.dataset['name'], null, null, null, enabled);
        } else {
            updateRoute(route.dataset['name'], null, null, null, null, null, enabled);
        }
    }
}

function isRouteEnabled(sinkName, sourceName) {
    const route = getRouteBySinkSource(sinkName, sourceName);
    if (route != null && route.dataset["enabled"].toLowerCase() == "true")
        return true;
    else 
        return false;
}

function editorEnableOnclick(e) {
    let node = e.target.parentNode.parentNode;
    node.className = node.className.replace("Disable", "Enable");
    node.dataset['routeedit'] = "enabled";
    drawLines();
}

function editorDisableOnclick(e) {
    let node = e.target.parentNode.parentNode;
    node.className = node.className.replace("Enable", "Disable");
    node.dataset['routeedit'] = "disabled";
    drawLines();
}


function editorSaveOnclick(e) {
    let target = e.target.parentNode.parentNode;
    if (target.dataset['type'] == 'SourceDescription') {
        saveSourceRoutes(target);
    }
    if (target.dataset['type'] == 'SinkDescription') {
        saveSinkRoutes(target);
    }
    setEditorActive(false);
    setTimeout(()=>{callApi("/body", "get", {}, restartCallback2);}, 1000);
}

function editorCancelOnclick(e) {
    setEditorActive(false);
    callApi("/body", "get", {}, restartCallback2);
}

function getSinkByName(name) {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    for (let sinkIdx in sinks)
        if (sinks[sinkIdx].dataset['name'] == name)
            return sinks[sinkIdx];
    return null;
}

function getSourceByName(name) {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    for (let sourceIdx in sources)
        if (sources[sourceIdx].dataset['name'] == name)
            return sources[sourceIdx];
    return null;
}

function updateRouteButtons() {
    const enableRoute = document.getElementById("button_enableRoute");
    const disableRoute = document.getElementById("button_disableRoute");
    const editRoute = document.getElementById("button_editRoute");
    const routeEqualizer = document.getElementById("button_routeEqualizer");
    const routeVolume = document.getElementById("routeVolume");
    if (enableRoute != null) {
        enableRoute.disabled = true;
        disableRoute.disabled = true;
        editRoute.disabled = true;
        routeEqualizer.disabled = true;
        routeVolume.disabled = true;
        routeVolume.value = 50;
    }
    enableRoute.style.display = "inline";
    disableRoute.style.display = "none";
    if (selectedSink && selectedSource) {
        const route = getRouteBySinkSource(selectedSink.dataset['name'], selectedSource.dataset['name']);
        if (isRouteEnabled(selectedSink.dataset['name'], selectedSource.dataset['name'])) {
            enableRoute.disabled = true;
            disableRoute.disabled = false;
            enableRoute.style.display = "none";
            disableRoute.style.display = "inline";
        } else {
            enableRoute.disabled = false;
            disableRoute.disabled = true;
        }
        editRoute.disabled = false;
        routeEqualizer.disabled = false;
        routeVolume.disabled = false;
        if (route != null)
            routeVolume.value = route.dataset['volume'] * 100;
    }
}

function enableRoute(e) {
    const route = getRouteBySinkSource(selectedSink.dataset['name'], selectedSource.dataset['name']);
    if (route != null)
        updateRoute(route.dataset['name'], null, null, null, null, null, true);
    else
        createRoute(selectedSource.dataset['name'] + " To " + selectedSink.dataset['name'], selectedSink.dataset['name'], selectedSource.dataset['name']);
}

function disableRoute(e) {
    const route = getRouteBySinkSource(selectedSink.dataset['name'], selectedSource.dataset['name']);
    if (route != null)
        updateRoute(route.dataset['name'], null, null, null, null, null, false);
}

function editRoute(e) {
    const route = getRouteBySinkSource(selectedSink.dataset['name'], selectedSource.dataset['name']);
    dialogUpdateRoute(route.dataset['name']);
}

function routeEqualizer(e) {
    const route = getRouteBySinkSource(selectedSink.dataset['name'], selectedSource.dataset['name']);
    dialogUpdateRouteEqualizer(route.dataset['name']);
}

function routeVolumeChange() {
    const route = getRouteBySinkSource(selectedSink.dataset['name'], selectedSource.dataset['name']);
    const routeVolume = document.getElementById("routeVolume");
    const volumeLevel = routeVolume.value / 100;
    callApi(`/routes/${route.dataset["name"]}/volume/${volumeLevel}`, "get");
}

export function onload() {
    exposeFunction(editorEnableOnclick, "editorEnableOnclick");
    exposeFunction(editorDisableOnclick, "editorDisableOnclick");
    exposeFunction(editorSaveOnclick, "editorSaveOnclick");
    exposeFunction(editorCancelOnclick, "editorCancelOnclick");
    exposeFunction(getSinkByName, "getSinkByName");
    exposeFunction(getSourceByName, "getSourceByName");
    exposeFunction(updateRouteButtons, "updateRouteButtons");
    exposeFunction(enableRoute, "enableRoute");
    exposeFunction(disableRoute, "disableRoute");
    exposeFunction(editRoute, "editRoute");
    exposeFunction(routeEqualizer, "routeEqualizer");
    exposeFunction(routeVolumeChange, "routeVolumeChange");
    exposeFunction(editSinkSources, "editSinkSources");
}