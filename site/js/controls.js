function add_source_button() {
    dialogAddSource();
}

function add_source_group_button() {
    dialogAddSourceGroup();
}

function add_sink_button() {
    dialogAddSink();
}

function add_sink_group_button() {
    dialogAddSinkGroup();
}

function add_route_button() {
    dialogAddRoute();
}

let selected_sink = "";
let selected_source = "";
let selected_route = "";

function volume_slider_change(e) {
    const volume_element = e.target;
    const option_element = e.target.parentNode.parentNode;
    const type = option_element.dataset["type"].replace("Description","").toLowerCase();
    const volume_level = volume_element.value / 100;
    call_api(`/${type}s/${option_element.dataset["name"]}/volume/${volume_level}`, "get");
}

function visualizer_icon_onclick(e) {
    const option = e.target.parentNode.parentNode;
    if (visual_playing) {
        stop_visualizer();
    } else {
        if (option.dataset["is_group"] !== "True") {
            start_visualizer(option.dataset["ip"]);
        } else {
            alert("Can't listen to group, must listen to sink endpoint");
        }
    }
}

function enable_disable_button(event) {
    let option = event.target.parentNode.parentNode;
    const type = option.dataset["type"].replace("Description","").toLowerCase();
    call_api("/" + type + "s/" + option.dataset["name"] + (option.dataset["enabled"] == "True"? "/disable" : "/enable"), 'get', {}, restart_callback);
}

function enable_disable_source_button() {
    const selected_sources = get_selected_sources();
    if (selected_sources.length !== 1) {
        alert("Exactly one source must be selected to edit");
        return;
    }
    const enable_disable = selected_sources[0].classList.contains("enabled") ? "/disable" : "/enable";
    call_api(`/sources/${selected_sources[0].dataset["name"]}${enable_disable}`, "get", 0, restart_callback);
}

function vnc_icon_onclick(e) {
    dialogVNC(e.target.parentNode.parentNode.dataset["name"]);
}

function playpause_icon_onclick(e) {
    call_api(`/sources/${e.target.parentNode.parentNode.dataset["name"]}/play`, "get", 0, null_callback);
}

function next_track_icon_onclick(e) {
    call_api(`/sources/${e.target.parentNode.parentNode.dataset["name"]}/nexttrack`, "get", 0, null_callback);
}

function previous_track_icon_onclick(e) {
    call_api(`/sources/${e.target.parentNode.parentNode.dataset["name"]}/prevtrack`, "get", 0, null_callback);
}

function equalizer_icon_onclick(e) {
    const parent_node = e.target.parentNode.parentNode;
    const type = parent_node.dataset["type"];
    const name = parent_node.dataset["name"];
    if (type === "SourceDescription") {
        dialogUpdateSourceEqualizer(name);
    } else if (type === "SinkDescription") {
        dialogUpdateSinkEqualizer(name);
    } else if (type === "RouteDescription") {
        dialogUpdateRouteEqualizer(name);
    }
}

function update_icon_onclick(e) {
    const parent_node = e.target.parentNode.parentNode;
    const type = parent_node.dataset["type"];
    const name = parent_node.dataset["name"];
    if (type === "SourceDescription") {
        dialogUpdateSource(name);
    } else if (type === "SinkDescription") {
        dialogUpdateSink(name);
    } else if (type === "RouteDescription") {
        dialogUpdateRoute(name);
    }
}

function volume_icon_onclick(e) {
    const parent_node = e.target.parentNode.parentNode;
    const type = parent_node.dataset["type"];
    let endpoint = "";
    let enabledisable = "";
    if (type === "SourceDescription" || type === "SinkDescription") {
        endpoint = type === "SourceDescription" ? "/sources/" : "/sinks/";
        enabledisable = parent_node.dataset["enabled"] === "True" ? "/disable" : "/enable";
    } else if (type === "RouteDescription") {
        endpoint = "/routes/";
        enabledisable = parent_node.dataset["enabled"] === "True" ? "/disable" : "/enable";
    }
    call_api(`${endpoint}${parent_node.dataset["name"]}${enabledisable}`, "get", 0, restart_callback);
}
function remove_icon_onclick(e) {
    const parent_node = e.target.parentNode.parentNode;
    const type = parent_node.dataset["type"];
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
    const do_it = confirm(`Are you sure you want to remove the ${confirmType} '${parent_node.dataset["name"]}'?`);
    if (do_it) {
        call_api(`${endpoint}${parent_node.dataset["name"]}`, "delete", 0, restart_callback);
    }
}

function listen_icon_onclick(e) {
    listen_to_sink_button(e.target.parentNode.parentNode);
}

function keypress_handler(event) {
    if (event.key === "Enter" || event.key === "Space") {
        if (event.target.onclick)
            event.target.onclick(event);
    }
}