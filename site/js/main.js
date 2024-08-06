function null_callback(response_text) {
}

function restart_callback(response_text) {
    call_api("/body", "get", {}, restart_callback_2)
}

function edit_source_callback(response_text) {
    var selected_sink_name = "";
    if (selected_sink)
        selected_sink_name = selected_sink.dataset['name'];
    var selected_source_name = "";
    if (selected_source)
        selected_source_name = selected_source.dataset['name'];
    var reload_div = document.getElementById("reload");
    reload_div.innerHTML = "";
    reload_div.innerHTML = response_text;
    var selected_sink_query = document.querySelectorAll("DIV#select-sinks SPAN[DATA-NAME='" + selected_sink_name + "']");
        if (selected_sink_query.length > 0)
            selected_sink = selected_sink_query[0];
        else
            selected_sink = "";
    if (selected_sink) highlight_active_sink();
    selected_source_query = document.querySelectorAll("DIV#select-sources SPAN[DATA-NAME='" + selected_source_name + "']");
    if (selected_source_query.length  >  0)
        selected_source  = selected_source_query[0];
    else
        selected_source  = "";
    if (selected_source) highlight_active_source(false);
    drawLines();
}

function edit_sink_callback(response_text) {
    var selected_sink_name = "";
    if (selected_sink)
        selected_sink_name = selected_sink.dataset['name'];
    var selected_source_name = "";
    if (selected_source)
        selected_source_name = selected_source.dataset['name'];
    var reload_div = document.getElementById("reload");
    reload_div.innerHTML = "";
    reload_div.innerHTML = response_text;
    var selected_sink_query = document.querySelectorAll("DIV#select-sinks SPAN[DATA-NAME='" + selected_sink_name + "']");
    if (selected_sink_query.length > 0)
        selected_sink = selected_sink_query[0];
    else
        selected_sink = "";
    if (selected_sink) highlight_active_sink(false);
    selected_source_query = document.querySelectorAll("DIV#select-sources SPAN[DATA-NAME='" + selected_source_name + "']");
    if (selected_source_query.length  >  0)
        selected_source  = selected_source_query[0];
    else
        selected_source  = "";
    if (selected_source) highlight_active_source();
    drawLines();
}

function restart_callback_2(response_text) {
    var selected_sink_name = "";
    if (selected_sink)
        selected_sink_name = selected_sink.dataset['name'];
    var selected_source_name = "";
    if (selected_source)
        selected_source_name = selected_source.dataset['name'];
    var selected_route_name = "";
    if (selected_route)
        selected_route_name = selected_route.dataset['name'];
    var selected_element_id = document.activeElement.id;
    var reload_div = document.getElementById("reload");
    reload_div.innerHTML = "";
    reload_div.innerHTML = response_text;
    var selected_sink_query = document.querySelectorAll("DIV#select-sinks SPAN[DATA-NAME='" + selected_sink_name + "']");
    if (selected_sink_query.length > 0)
        selected_sink = selected_sink_query[0];
    else
        selected_sink = "";
    selected_source_query = document.querySelectorAll("DIV#select-sources SPAN[DATA-NAME='" + selected_source_name + "']");
    if (selected_source_query.length  >  0)
        selected_source  = selected_source_query[0];
    else
        selected_source  = "";
    selected_route_query = document.querySelectorAll("DIV#select-routes SPAN[DATA-NAME='"  + selected_route_name  + "']");
    if (selected_route_query.length > 0)
        selected_route = selected_route_query[0];
    else
        selected_route = "";
    if (selected_sink) highlight_active_sink();
    if (selected_source) highlight_active_source();
    highlight_active_routes();
    var selected_element_query = document.getElementById(selected_element_id);
    if (selected_element_query)
        selected_element_query.focus();
    dialogCancel();
    dismissShadow();
}
