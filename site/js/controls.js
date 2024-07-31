function add_source_button() {
    dialog_add_source();
}

function add_source_group_button() {
    dialog_add_source_group();
}

function add_sink_button() {
    dialog_add_sink();
}

function add_sink_group_button() {
    dialog_add_sink_group();
}

function add_route_button() {
    dialog_add_route();
}

let selected_sink = "";
function sink_volume_change() {
    const volume_element = document.getElementById("sink_volume");
    const volume_level = volume_element.value / 100;
    selected_sink.dataset["volume"] = volume_level;
    call_api(`/sinks/${selected_sink.dataset["name"]}/volume/${volume_level}`, "get");
}

let selected_source = "";
function source_volume_change() {
    const volume_element = document.getElementById("source_volume");
    const volume_level = volume_element.value / 100;
    selected_source.dataset["volume"] = volume_level;
    call_api(`/sources/${selected_source.dataset["name"]}/volume/${volume_level}`, "get");
}

let selected_route = "";
function route_volume_change() {
    const volume_element = document.getElementById("route_volume");
    const volume_level = volume_element.value / 100;
    selected_route.dataset["volume"] = volume_level;
    call_api(`/routes/${selected_route.dataset["name"]}/volume/${volume_level}`, "get");
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
    dialog_vnc(e.target.parentNode.parentNode.dataset["name"]);
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
        dialog_update_source_equalizer(name);
    } else if (type === "SinkDescription") {
        dialog_update_sink_equalizer(name);
    } else if (type === "RouteDescription") {
        dialog_update_route_equalizer(name);
    }
}

function update_icon_onclick(e) {
    const parent_node = e.target.parentNode.parentNode;
    const type = parent_node.dataset["type"];
    const name = parent_node.dataset["name"];
    if (type === "SourceDescription") {
        dialog_update_source(name);
    } else if (type === "SinkDescription") {
        dialog_update_sink(name);
    } else if (type === "RouteDescription") {
        dialog_update_route(name);
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
        event.target.onclick(event);
    }
}