let editorActive = false;
let editorType = "";
function getRouteByName(name) {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    for (routeidx in routes)
        if (routes[routeidx].dataset['name'] == name)
            return routes[routeidx];
    return null;
}

function getRouteBySinkSource(sink, source) {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    for (routeidx in routes)
        if (routes[routeidx].dataset['sink'] == sink && routes[routeidx].dataset['source'] == source)
            return routes[routeidx];
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
    call_api(url, method, route, restart_callback);
}

function updateRoute(name, sink=null, source=null, volume=null, equalizer=null, delay=null, enabled=null) {
        let routeElement = getRouteByName(name);
        if (sink == null) sink = routeElement.dataset['sink'];
        if (source == null) source = routeElement.dataset['source'];
        if (volume == null) volume = routeElement.dataset['volume'];
        if (equalizer == null) { 
            let equalizer_json = "{" + routeElement.dataset['equalizer'].replace(/b(\d+)=(\d+\.?\d*)/g, '"b$1": $2,') + "}";
            equalizer_json = equalizer_json.replace(",}", "}");
            equalizer = JSON.parse(equalizer_json);
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
        call_api(url, method, route, restart_callback);
}

function editSinkSources(e) {
    editorActive = true;
    let tgt = e.target.parentNode.parentNode;
    const type = tgt.dataset["type"].replace("Description","").toLowerCase();
    editorType = type;
    if (type === "source")
        highlightSinksBasedOffSource(tgt);
    if (type === "sink")
        highlightSourcesBasedOffSink(tgt);
}

function highlightSinksBasedOffSource(source) {
    call_api("site/edit_source_routes/" + source.dataset['name'], "get", {}, edit_sink_callback);
}

function highlightSourcesBasedOffSink(sink) {
    call_api("site/edit_sink_routes/" + sink.dataset['name'], "get", {}, edit_source_callback);
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

function saveSourceRoutes(source) {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    for (sinkidx in sinks) {
        const sink = sinks[sinkidx];
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
    for (sourceidx in sources) {
        const source = sources[sourceidx];
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

function editorSaveOnclick(e) {
    let target = e.target.parentNode.parentNode;
    if (target.dataset['type'] == 'SourceDescription') {
        saveSourceRoutes(target);
    }
    if (target.dataset['type'] == 'SinkDescription') {
        saveSinkRoutes(target);
    }
    editorActive = false;
    setTimeout(()=>{call_api("/body", "get", {}, restart_callback_2);}, 1000);
}

function editorCancelOnclick(e) {
    editorActive = false;
    call_api("/body", "get", {}, restart_callback_2);
}

function getSinkByName(name) {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    for (sinkidx in sinks)
        if (sinks[sinkidx].dataset['name'] == name)
            return sinks[sinkidx];
    return null;
}

function getSourceByName(name) {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    for (sourceidx in sources)
        if (sources[sourceidx].dataset['name'] == name)
            return sources[sourceidx];
    return null;
}

function isRouteEnabled(sinkName, sourceName) {
    const route = getRouteBySinkSource(sinkName, sourceName);
    if (route != null && route.dataset["enabled"].toLowerCase() == "true")
        return true;
    else 
        return false;
}

function updateRouteButtons() {
    const enableRoute = document.getElementById("enable_route");
    const disableRoute = document.getElementById("disable_route");
    const editRoute = document.getElementById("edit_route");
    const routeEqualizer = document.getElementById("route_equalizer");
    const routeVolume = document.getElementById("route_volume");
    if (enableRoute != null) {
        enableRoute.disabled = true;
        disableRoute.disabled = true;
        editRoute.disabled = true;
        routeEqualizer.disabled = true;
        route_volume.disabled = true;
        route_volume.value = 50;
        enableRoute.style.display = "inline";
        disableRoute.style.display = "none";
    }
    if (selected_sink && selected_source) {
        const route = getRouteBySinkSource(selected_sink.dataset['name'], selected_source.dataset['name']);
        if (isRouteEnabled(selected_sink.dataset['name'], selected_source.dataset['name'])) {
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
        route_volume.disabled = false;
        route_volume.value = route.dataset['volume'] * 100;
    }
}

function enable_route(e) {
    const route = getRouteBySinkSource(selected_sink.dataset['name'], selected_source.dataset['name']);
    if (route != null)
        updateRoute(route.dataset['name'], null, null, null, null, null, true);
    else
        createRoute(selected_sink.dataset['name'] + " To " + selected_source.dataset['name'], selected_sink.dataset['name'], selected_source.dataset['name']);
}

function disable_route(e) {
    const route = getRouteBySinkSource(selected_sink.dataset['name'], selected_source.dataset['name']);
    if (route != null)
        updateRoute(route.dataset['name'], null, null, null, null, null, false);
}

function edit_route(e) {
    const route = getRouteBySinkSource(selected_sink.dataset['name'], selected_source.dataset['name']);
    dialogUpdateRoute(route.dataset['name']);
}

function route_equalizer(e) {
    const route = getRouteBySinkSource(selected_sink.dataset['name'], selected_source.dataset['name']);
    dialogUpdateRouteEqualizer(route.dataset['name']);
}

function route_volume_change() {
    const route = getRouteBySinkSource(selected_sink.dataset['name'], selected_source.dataset['name']);
    const routeVolume = document.getElementById("route_volume");
    const volume_level = routeVolume.value / 100;
    call_api(`/routes/${route.dataset["name"]}/volume/${volume_level}`, "get");
}