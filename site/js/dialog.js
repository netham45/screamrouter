function show_dialog_callback(response_text) {
    show_shadow();
    document.getElementById("dialog").style.display = "block";
    document.getElementById("dialog").innerHTML = response_text;
    document.getElementById("dialog").scrollIntoView();
}

async function call_dialog(url, source_name = "") {
    let full_url = "site/" + url;
    if (source_name) {
        full_url += "/" + source_name;
    }
    call_api(full_url, "GET", "", show_dialog_callback);
    if (url === "vnc") {
        await new Promise(r => setTimeout(r, 500));
        if (matchMedia('(pointer:coarse)').matches)
            document.getElementById("vnc-iframe").requestFullscreen();
    }
}

function dialog_add_source() {
    call_dialog("add_source");
}

function dialog_add_source_group() {
    call_dialog("add_source_group");
}

function dialog_vnc(source_name) {
    call_dialog("vnc", source_name);
}

function dialog_update_source(source_name) {
    call_dialog("edit_source", source_name);
}

function dialog_update_source_equalizer(source_name) {
    call_dialog("edit_source", source_name + "/equalizer");
}

function dialog_add_sink() {
    call_dialog("add_sink");
}

function dialog_add_sink_group() {
    call_dialog("add_sink_group");
}

function dialog_update_sink(sink_name) {
    call_dialog("edit_sink", sink_name);
}

function dialog_update_sink_equalizer(sink_name) {
    call_dialog("edit_sink", sink_name + "/equalizer");
}

function dialog_add_route() {
    call_dialog("add_route");
}

function dialog_update_route(route_name) {
    call_dialog("edit_route", route_name);
}

function dialog_update_route_equalizer(route_name) {
    call_dialog("edit_route", route_name + "/equalizer");
}

function dialog_cancel() {
    document.getElementById("dialog").style.display = "none";
    document.getElementById("dialog").innerHTML = "";
    dismiss_shadow();
}

function dialog_equalizer_default() {
    document.querySelectorAll("DIV#dialog INPUT[TYPE='range']").forEach(input => input.value = 100);
}

function dialog_submit(close) {
    const result = {};
    let url = "";
    let action = "";
    const equalizer = {};

    document.querySelectorAll("DIV#dialog INPUT, DIV#dialog SELECT").forEach(entry => {
        if (entry.id == undefined) return;
        if (entry.type == "checkbox") {
            result[entry.id] = entry.checked;
        } else if (entry.type == "select-multiple") {
            result[entry.id] = Array.from(entry.selectedOptions).map(option => option.value).filter(value => value != undefined);
        } else if (entry.id == "dialog_url") {
            url = entry.value;
        } else if (entry.id == "dialog_action") {
            action = entry.value;
        } else if (entry.id.startsWith("eq_b")) {
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
    call_api(url, action, result, close ? restart_callback : undefined);
}
function dialog_submit_close() {
    dialog_submit(true);
}

function dialog_submit_noclose() {
    dialog_submit(false);
}

function show_shadow() {
    document.getElementById("shadow").style.display = "block";
    document.getElementById("enable_disable").inert = true;
}

function dismiss_shadow() {
    document.getElementById("shadow").style.display = "none";
    document.getElementById("enable_disable").inert = false;
}