function showDialogCallback(response_text) {
    showShadow();
    document.getElementById("dialog").style.display = "block";
    document.getElementById("dialog").innerHTML = response_text;
    document.getElementById("dialog").scrollIntoView();
}

async function callDialog(url, source_name = "") {
    let full_url = "site/" + url;
    if (source_name) {
        full_url += "/" + source_name;
    }
    call_api(full_url, "GET", "", showDialogCallback);
    if (url === "vnc") {
        await new Promise(r => setTimeout(r, 500));
        if (matchMedia('(pointer:coarse)').matches)
            document.getElementById("vnc-iframe").requestFullscreen();
    }
}

function dialogAddSource() {
    callDialog("add_source");
}

function dialogAddSourceGroup() {
    callDialog("add_source_group");
}

function dialogVNC(source_name) {
    callDialog("vnc", source_name);
}

function dialogUpdateSource(source_name) {
    callDialog("edit_source", source_name);
}

function dialogUpdateSourceEqualizer(source_name) {
    callDialog("edit_source", source_name + "/equalizer");
}

function dialogAddSink() {
    callDialog("add_sink");
}

function dialogAddSinkGroup() {
    callDialog("add_sink_group");
}

function dialogUpdateSink(sink_name) {
    callDialog("edit_sink", sink_name);
}

function dialogUpdateSinkEqualizer(sink_name) {
    callDialog("edit_sink", sink_name + "/equalizer");
}

function dialogAddRoute() {
    callDialog("add_route");
}

function dialogUpdateRoute(route_name) {
    callDialog("edit_route", route_name);
}

function dialogUpdateRouteEqualizer(route_name) {
    callDialog("edit_route", route_name + "/equalizer");
}

function dialogCancel() {
    document.getElementById("dialog").style.display = "none";
    document.getElementById("dialog").innerHTML = "";
    dismissShadow();
}

function dialogEqualizerDefault() {
    document.querySelectorAll("DIV#dialog INPUT[TYPE='range']").forEach(input => input.value = 100);
}

function dialogSubmit(close) {
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
    console.log("Calling API");
    call_api(url, action, result, close ? restart_callback : undefined);
}

function dialogSubmitClose() {
    dialogSubmit(true);
}

function dialogSubmitNoClose() {
    dialogSubmit(false);
}

function showShadow() {
    document.getElementById("shadow").style.display = "block";
    document.getElementById("enable_disable").inert = true;
}

function dismissShadow() {
    document.getElementById("shadow").style.display = "none";
    document.getElementById("enable_disable").inert = false;
}