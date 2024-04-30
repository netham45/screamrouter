const BASEURL = "/"
var sinks = {}
var sources = {}
var routes = {}

function call_api(endpoint, method, data, callback) {
    console.log(data)
    const xhr = new XMLHttpRequest();
    xhr.open(method, BASEURL + endpoint, true);
    xhr.getResponseHeader("Content-type", "application/json");
    if (method == "POST" || method == "PUT")
        xhr.setRequestHeader("Content-Type", "application/json;charset=UTF-8");
        xhr.send(data)

    xhr.onload = function() {
        const obj = JSON.parse(this.responseText);
        if (obj["error"])
            alert(obj["error"])
        else if (obj["detail"])
            alert(JSON.stringify(obj["detail"]))
        else
            callback(obj)
    }
}

function create_option(name, label, id, ip = "", is_group = false) {
    option = document.createElement("option");
    option.name = name;
    option.innerHTML = label;
    option.value = id;
    option.ip = ip;
    option.is_group = is_group;
    return option;
}

// API Calls

function populate_sinks() {
    call_api("sinks", "GET", "", populate_sinks_callback);
}

function populate_sinks_callback(data) {
    sinks = data
    for (entry in data)
        document.getElementById("sinks").appendChild(create_option(data[entry].name, data[entry].name + (data[entry].is_group?" [" + data[entry].group_members.join(", ") + "]":"") + (data[entry].enabled?"[Enabled]":"[Disabled]"), entry, data[entry].ip, data[entry].is_group));
}

function populate_sources() {
    call_api("sources", "GET", "", populate_sources_callback);
}

function populate_sources_callback(data) {
    sources = data
    for (entry in data)
        document.getElementById("sources").appendChild(create_option(data[entry].name, data[entry].name + (data[entry].is_group?" [" + data[entry].group_members.join(", ") + "]":"") + (data[entry].enabled?"[Enabled]":"[Disabled]"), entry,data[entry].ip,data[entry].is_group));
}

function populate_routes() {
    call_api("routes", "GET", "", populate_routes_callback);
}

function populate_routes_callback(data){
    routes = data
    for (entry in data)
        document.getElementById("routes").appendChild(create_option(data[entry].name, data[entry].name + " [Sink: " + data[entry].sink + " Source: " + data[entry].source + "]" + (data[entry].enabled?"[Enabled]":"[Disabled]"), entry, null, false));
}

function remove_sink(sink_name) {
    call_api("sinks/" + sink_name, "DELETE", "", reload_callback);
}

function remove_source(source_name) {
    call_api("sources/" + source_name, "DELETE", "", reload_callback);
}

function remove_route(route_name) {
    call_api("routes/" + route_name, "DELETE", "", reload_callback);
}

function disable_sink(sink_name) {
    call_api("sinks/" + sink_name + "/disable", "GET", "", reload_callback);
}

function enable_sink(sink_name) {
    call_api("sinks/" + sink_name + "/enable", "GET", "", reload_callback);
}

function disable_source(source_name) {
  call_api("sources/" + source_name + "/disable", "GET", "", reload_callback);
}

function enable_source(source_name) {
    call_api("sources/" + source_name + "/enable", "GET", "", reload_callback);
}

function disable_route(route_name) {
    call_api("routes/" + route_name + "/disable", "GET", "", reload_callback);
}

function enable_route(route_name) {
    call_api("routes/" + route_name + "/enable", "GET", "", reload_callback);
}


function reload_callback(data)
{
    load();
}

function null_callback(data)
{

}

// Buttons

function add_source_button() {
  name = window.prompt("Provide a source name")
  if (name == null)
    return
  ip = window.prompt("Provide an IP (Ex. 192.168.0.100)")
  if (ip == null)
    return
  data = {"name": name, "ip": ip};
  call_api("sources", "POST", JSON.stringify(data), reload_callback);
}

function remove_source_button() {
    options = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
    options.reverse().forEach(function (option){
        remove_source(option.name);
    });
}

function disable_source_button() {
    options = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
    options.reverse().forEach(function (option){
        disable_source(option.name);
    });
}

function enable_source_button() {
    options = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
    options.reverse().forEach(function (option){
        enable_source(option.name);
    });
}

function add_source_group_button() {
    options = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
    labels = []
    options.reverse().forEach(function (option){
      labels.push(option.name);
    });
    name = window.prompt("Provide a source group name")
    if (name == null)
        return
    data = {"name": name, "sources": labels};
    call_api("groups/sources/", "POST", JSON.stringify(data), reload_callback);
}

function add_sink_button() {
    name = window.prompt("Provide a sink name")
    if (name == null)
    return
    ip = window.prompt("Provide an IP (Ex. 192.168.0.100)")
    if (ip == null)
    return
    port = window.prompt("Provide a Port (Ex. 4011)", 4011)
    if (port == null)
    return
    data = {"name": name, "ip": ip, "port": port};
    call_api("sinks", "POST", JSON.stringify(data), reload_callback);
}

function remove_sink_button() {
    options = [... document.querySelectorAll("SELECT#sinks OPTION:checked")]
    options.reverse().forEach(function (option){
        remove_sink(option.name);
    });
}

function disable_sink_button() {
    options = [... document.querySelectorAll("SELECT#sinks OPTION:checked")]
    options.reverse().forEach(function (option){
        disable_sink(option.name);
    });
}

function enable_sink_button() {
    options = [... document.querySelectorAll("SELECT#sinks OPTION:checked")]
    options.reverse().forEach(function (option){
        enable_sink(option.name);
    });
}

function add_sink_group_button(){
    options = [... document.querySelectorAll("SELECT#sinks OPTION:checked")]
    labels = []
    options.reverse().forEach(function (option){
      labels.push(option.name);
    });
    name = window.prompt("Provide a sink group name")
    if (name == null)
        return
    data = {"name": name, "sinks": labels};
    call_api("groups/sinks/", "POST", JSON.stringify(data), reload_callback);
}

function add_route_button() {
    sourceOptions = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
    sinkOptions = [... document.querySelectorAll("SELECT#sinks OPTION:checked")]
    if ( sourceOptions.length != 1 || sinkOptions.length != 1) {
        alert("Select one source and sink per route");
        return false;
    }
    name = window.prompt("Provide a sink group name")
    if (name == null)
        return
    data = {"name": name, "source": sourceOptions[0].name, "sink": sinkOptions[0].name};
    call_api("routes", "POST", JSON.stringify(data), reload_callback);
}

function remove_route_button() {
    options = [... document.querySelectorAll("SELECT#routes OPTION:checked")]
    options.reverse().forEach(function (option){
        remove_route(option.name);
    });
}

function disable_route_button() {
    options = [... document.querySelectorAll("SELECT#routes OPTION:checked")]
    options.reverse().forEach(function (option){
        disable_route(option.name);
    });
}

function enable_route_button() {
    options = [... document.querySelectorAll("SELECT#routes OPTION:checked")]
    options.reverse().forEach(function (option){
        enable_route(option.name);
    });
}

// Handle updating volume slider when different fields are selected

function sources_onchange() {
  checkedoptions = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
  volumeslider = document.getElementById("source_volume")
  console.log(checkedoptions)
  if ( checkedoptions.length == 1 )
  {
      for (source in sources) {
         if (checkedoptions[0].name == sources[source].name) {
            volumeslider.disabled = false;
            volumeslider.value = sources[source].volume * 100;
         }
      }
  }
  else
  {
      volumeslider.disabled = true;
      volumeslider.value = 50;
  }
}

function sinks_onchange() {
  checkedoptions = [... document.querySelectorAll("SELECT#sinks OPTION:checked")]
  volumeslider = document.getElementById("sink_volume")
  if ( checkedoptions.length == 1 )
  {
      for (sink in sinks) {
         if (checkedoptions[0].name == sinks[sink].name) {
            volumeslider.disabled = false;
            volumeslider.value = sinks[sink].volume * 100;
         }
      }
  }
  else
  {
      volumeslider.disabled = true;
      volumeslider.value = 50;
  }
}

function routes_onchange() {
  checkedoptions = [... document.querySelectorAll("SELECT#routes OPTION:checked")]
  volumeslider = document.getElementById("route_volume")
  console.log(checkedoptions)
  if ( checkedoptions.length == 1 )
  {
      for (route in routes) {
         if (checkedoptions[0].name == routes[route].name) {
            volumeslider.disabled = false;
            volumeslider.value = routes[route].volume * 100;
         }
      }
  }
  else
  {
      volumeslider.disabled = true;
      volumeslider.value = 50;
  }
}

// Volume Sliders

function source_volume_onchange() {
    checkedoptions = [... document.querySelectorAll("SELECT#sources OPTION:checked")]
    volumeslider = document.getElementById("source_volume")
    if ( checkedoptions.length == 1 )
    {
        for (source in sources) {
           if (checkedoptions[0].name == sources[source].name) {
              call_api("sources/" + sources[source].name + "/volume/" + (volumeslider.value / 100), "GET", "", null_callback);
              sources[source].volume = (volumeslider.value / 100)
           }
        }
    }
    else
    {
        volumeslider.disabled = true;
    }
}

function sink_volume_onchange() {
    checkedoptions = [... document.querySelectorAll("SELECT#sinks OPTION:checked")];
    volumeslider = document.getElementById("sink_volume");
    if ( checkedoptions.length == 1 ) {
        for (sink in sinks) {
            if (checkedoptions[0].name == sinks[sink].name) {
            call_api("sinks/" + sinks[sink].name + "/volume/" + (volumeslider.value / 100), "GET", "", null_callback);
            sinks[sink].volume = (volumeslider.value / 100);
            }
        }
    }
    else
    {
        volumeslider.disabled = true;
    }
}

function route_volume_onchange() {
    checkedoptions = [... document.querySelectorAll("SELECT#routes OPTION:checked")]
    volumeslider = document.getElementById("route_volume")
    if ( checkedoptions.length == 1 )
    {
        for (route in routes) {
            if (checkedoptions[0].name == routes[route].name) {
            call_api("routes/" + routes[route].name + "/volume/" + (volumeslider.value / 100), "GET", "", null_callback);
            routes[route].volume = (volumeslider.value / 100)
            }
        }
    }
    else
    {
        volumeslider.disabled = true;
    }
}

function load()
{
    [... document.querySelectorAll("table.main option")].forEach(function(option){option.parentNode.removeChild(option)})
    populate_sources();
    populate_sinks();
    populate_routes();
    sources_onchange();
    sinks_onchange();
    routes_onchange();
}

window.addEventListener("load", (event) => {
    load();
});

audio = {};
audio_playing = false;

function button_listen() {
    checkedoptions = [... document.querySelectorAll("SELECT#sinks OPTION:checked")];
    if (audio_playing)
    {
        stop_audio();
    }
    else if (checkedoptions.length == 1)
    {
        if (!checkedoptions[0].is_group)
        {
            start_audio(checkedoptions[0].ip);
        }
        else
            alert("Can't listen to group, must listen to sink endpoint");
    }
    else
    {
        alert("Select one sink to listen from");
    }
}

function start_audio(sink_ip) {
    audiotag = document.getElementById("audio")
    audiotag.pause();
    audiotag.src = "";
    audiotag.src = 'http://192.168.3.114:8080/stream/' + sink_ip + '/';
    audiotag.play();
    audiotag.style.display = "inline";
    audio_playing = true;
    button = document.getElementById("button_listen").value = "Stop playback";
}

function stop_audio() {
    audiotag = document.getElementById("audio")
    audiotag.pause();
    audiotag.style.display = "none";
    audio_playing = false;
    button = document.getElementById("button_listen").value = "Listen to sink";
}

function show_shadow() {
    shadow = document.getElementById("shadow");
    shadow.style.display = "block";
}

function dismiss_shadow() {
    shadow = document.getElementById("shadow");
    shadow.style.display = "none";
}

function shadow_div() {
    //dismiss_dialog();
}

function dismiss_dialog() {
    dialogs = [... document.getElementsByClassName("dialog")];
    dialogs.forEach(function(dialog){dialog.style.display = "none";})
    dismiss_shadow();
}

function open_dialog(name) {
    var dialog = document.getElementById(name)
    dialog.style.display = "block";
    show_shadow();
}

function set_field_value(dialogname, fieldname, fieldvalue) {
    console.log(dialogname)
    console.log(fieldname)
    console.log(fieldvalue)
    console.log("Querying for " + "div#" + dialogname + " input#" + fieldname)
    var field = document.querySelectorAll("div#" + dialogname + " input#" + fieldname)[0]
    console.log(field)
    field.value = fieldvalue
}

function get_field_value(dialogname, fieldname) {
    var field = document.querySelectorAll("div#" + dialogname + " input#" + fieldname)[0]
    return field.value
}

function set_select_selected(select, value) {
    for (index in select.children)
        console.log("Comparing" +select.children[index].value + " == " + value);
        if (select.children[index].value == value)
            select.selectedIndex = index;
}

function get_select_selected(select) {
    return select.children[select.selectedIndex].value
}

function get_selected_sink() {
    var checkedoptions = [... document.querySelectorAll("SELECT#sinks OPTION:checked")];
    if (checkedoptions.length == 1) {
        return checkedoptions[0];
    }
    return null;
}

function get_selected_source() {
    var checkedoptions = [... document.querySelectorAll("SELECT#sources OPTION:checked")];
    if (checkedoptions.length == 1) {
        return checkedoptions[0];
    }
    return null;
}

function get_selected_route() {
    var checkedoptions = [... document.querySelectorAll("SELECT#routes OPTION:checked")];
    if (checkedoptions.length == 1) {
        return checkedoptions[0];
    }
    return null;
}

function get_source_info(name) {
    for (source in sources)
        if (sources[source].name == name)
            return sources[source];
}

function get_sink_info(name) {
    for (sink in sinks)
        if (sinks[sink].name == name)
            return sinks[sink];
}

function get_route_info(name) {
    for (route in routes)
        if (routes[route].name == name)
            return routes[route];
}

function add_source_button() {
    var dialog = "add_source"
    open_dialog(dialog);
    set_field_value(dialog, "sourcename", "")
    set_field_value(dialog, "sourceip", "")
}

function update_source_button() {
    var source = get_selected_source();
    if (source === null) {
        alert("Select a source to edit");
        return
    }
    var sourceinfo = get_source_info(source.name);
    if (sourceinfo.is_group)
    {
        alert("Can't edit groups yet");
        return
    }

    var dialog = "update_source"
    open_dialog(dialog);
    set_field_value(dialog, "sourcename", source.name)
    set_field_value(dialog, "sourceip", source.ip)
}

function add_sink_button() {
    open_dialog("add_sink");
}

function update_sink_button() {
    var sink = get_selected_sink();
    if (sink === null) {
        alert("Select a sink to edit");
        return
    }
    var sinkinfo = get_sink_info(sink.name);
    if (sinkinfo.is_group)
    {
        alert("Can't edit groups yet");
        return
    }
    console.log(sinkinfo);
    dialog = "update_sink"
    open_dialog(dialog);
    set_field_value(dialog, "sinkname", sinkinfo.name);
    set_field_value(dialog, "sinkip", sinkinfo.ip);
    set_field_value(dialog, "sinkport", sinkinfo.port);
    set_field_value(dialog, "sinkchannels", sinkinfo.channels);
    set_field_value(dialog, "sinkdelay", sinkinfo.delay);
    var bitdepthselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinkbitdepth")][0];
    var samplerateselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinksamplerate")][0];
    var channellayoutselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinkchannellayout")][0];
    set_select_selected(bitdepthselect, sinkinfo.bit_depth);
    set_select_selected(samplerateselect, sinkinfo.sample_rate);
    set_select_selected(channellayoutselect, sinkinfo.channel_layout);
}

function update_sink_equalizer_button() {
    var sink = get_selected_sink();
    if (sink === null) {
        alert("Select a sink to edit");
        return
    }
    var sinkinfo = get_sink_info(sink.name);
    if (sinkinfo.is_group)
    {
        alert("Can't edit groups yet");
        return
    }
    console.log(sinkinfo);
    dialog = "update_sink_equalizer"
    open_dialog(dialog);
    set_field_value(dialog, "sinkname", sinkinfo.name);
    set_field_value(dialog, "eq_b1", 200 - sinkinfo.equalizer.b1 * 100);
    set_field_value(dialog, "eq_b2", 200 - sinkinfo.equalizer.b2 * 100);
    set_field_value(dialog, "eq_b3", 200 - sinkinfo.equalizer.b3 * 100);
    set_field_value(dialog, "eq_b4", 200 - sinkinfo.equalizer.b4 * 100);
    set_field_value(dialog, "eq_b5", 200 - sinkinfo.equalizer.b5 * 100);
    set_field_value(dialog, "eq_b6", 200 - sinkinfo.equalizer.b6 * 100);
    set_field_value(dialog, "eq_b7", 200 - sinkinfo.equalizer.b7 * 100);
    set_field_value(dialog, "eq_b8", 200 - sinkinfo.equalizer.b8 * 100);
    set_field_value(dialog, "eq_b9", 200 - sinkinfo.equalizer.b9 * 100);
    set_field_value(dialog, "eq_b10", 200 - sinkinfo.equalizer.b10 * 100);
    set_field_value(dialog, "eq_b11", 200 - sinkinfo.equalizer.b11 * 100);
    set_field_value(dialog, "eq_b12", 200 - sinkinfo.equalizer.b12 * 100);
    set_field_value(dialog, "eq_b13", 200 - sinkinfo.equalizer.b13 * 100);
    set_field_value(dialog, "eq_b14", 200 - sinkinfo.equalizer.b14 * 100);
    set_field_value(dialog, "eq_b15", 200 - sinkinfo.equalizer.b15 * 100);
    set_field_value(dialog, "eq_b16", 200 - sinkinfo.equalizer.b16 * 100);
    set_field_value(dialog, "eq_b17", 200 - sinkinfo.equalizer.b17 * 100);
    set_field_value(dialog, "eq_b18", 200 - sinkinfo.equalizer.b18 * 100);
}

function default_eq_button() {
    dialog = "update_sink_equalizer"
    set_field_value(dialog, "eq_b1", 100);
    set_field_value(dialog, "eq_b2", 100);
    set_field_value(dialog, "eq_b3", 100);
    set_field_value(dialog, "eq_b4", 100);
    set_field_value(dialog, "eq_b5", 100);
    set_field_value(dialog, "eq_b6", 100);
    set_field_value(dialog, "eq_b7", 100);
    set_field_value(dialog, "eq_b8", 100);
    set_field_value(dialog, "eq_b9", 100);
    set_field_value(dialog, "eq_b10", 100);
    set_field_value(dialog, "eq_b11", 100);
    set_field_value(dialog, "eq_b12", 100);
    set_field_value(dialog, "eq_b13", 100);
    set_field_value(dialog, "eq_b14", 100);
    set_field_value(dialog, "eq_b15", 100);
    set_field_value(dialog, "eq_b16", 100);
    set_field_value(dialog, "eq_b17", 100);
    set_field_value(dialog, "eq_b18", 100);
}

function add_route_button() {
    var dialog = "add_route";
    open_dialog(dialog);
    var sinkselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesink")][0];
    var sourceselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesource")][0];
    populate_route_sinks(sinkselect);
    populate_route_sources(sourceselect);
}

function update_route_button() {
    var route = get_selected_route();
    if (route === null) {
        alert("Select a route to edit");
        return
    }
    
    var routeinfo = get_route_info(route.name);
    if (routeinfo.is_group)
    {
        alert("Can't edit groups yet");
        return
    }

    var dialog = "update_route";
    open_dialog(dialog);
    var sinkselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesink")][0];
    var sourceselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesource")][0];
    set_field_value(dialog, "routename", route.name)
    populate_route_sinks(sinkselect);
    populate_route_sources(sourceselect);
    for (index in sinkselect.children) {
        if (sinkselect.children[index].name == routeinfo.sink) {
            sinkselect.selectedIndex = index;
        }
    }
    for (index in sourceselect.children) {
        if (sourceselect.children[index].name == routeinfo.source) {
            sourceselect.selectedIndex = index;
        }
    }
}

function populate_route_sinks(selecttag) {
    while (selecttag.hasChildNodes()) {
        selecttag.removeChild(selecttag.lastChild);
    }
    for (index in sinks) {
        sink = sinks[index];
        selecttag.appendChild(create_option(sink.name, sink.name, sink.name))
    }
}

function populate_route_sources(selecttag) {
    while (selecttag.hasChildNodes()) {
        selecttag.removeChild(selecttag.lastChild);
    }
    for (index in sources) {
        source = sources[index];
        selecttag.appendChild(create_option(source.name, source.name, source.name))
    }
}

function do_add_source() {
    dialog = "add_source";
    var data = {
        "name": get_field_value(dialog, "sourcename"),
        "ip": get_field_value(dialog, "sourceip")
    };
    call_api("sources", "POST", JSON.stringify(data), reload_callback);
    dismiss_dialog();
}

function do_update_source() {
    dialog = "update_source";
    var data = {
        "name": get_field_value(dialog, "sourcename"),
        "ip": get_field_value(dialog, "sourceip")
    };
    call_api("sources", "PUT", JSON.stringify(data), reload_callback);
    dismiss_dialog();
}

function do_add_sink() {
    dialog = "add_sink"
    var bitdepthselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinkbitdepth")][0];
    var samplerateselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinksamplerate")][0];
    var channellayoutselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinkchannellayout")][0];
    var data = {
        "ip": get_field_value(dialog, "sinkip"),
        "bit_depth": parseInt(get_select_selected(bitdepthselect)),
        "sample_rate": parseInt(get_select_selected(samplerateselect)),
        "channels": parseInt(get_field_value(dialog, "sinkchannels")),
        "channel_layout": get_select_selected(channellayoutselect),
        "delay": int(get_field_value(dialog, "sinkdelay")),
        "equalizer": {
            "b1": 1,
            "b2": 1,
            "b3": 1,
            "b4": 1,
            "b5": 1,
            "b6": 1,
            "b7": 1,
            "b8": 1,
            "b9": 1,
            "b10": 1,
            "b11": 1,
            "b12": 1,
            "b13": 1,
            "b14": 1,
            "b15": 1,
            "b16": 1,
            "b17": 1,
            "b18": 1
        }
        
    };
    call_api("sinks/"+get_field_value(dialog, "sinkname"), "POST", data, reload_callback);
    dismiss_dialog();
}

function do_update_sink() {
    dialog = "update_sink"
    var bitdepthselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinkbitdepth")][0];
    var samplerateselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinksamplerate")][0];
    var channellayoutselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#sinkchannellayout")][0];
    sinkinfo = {}
    for (sink in sinks) {
        if (sinks[sink].name == get_field_value(dialog, "sinkname"))
            sinkinfo = sinks[sink]
    }
    var data = {
        "ip": get_field_value(dialog, "sinkip"),
        "port": parseInt(get_field_value(dialog, "sinkport")),
        "bit_depth": parseInt(get_select_selected(bitdepthselect)),
        "sample_rate": parseInt(get_select_selected(samplerateselect)),
        "channels": parseInt(get_field_value(dialog, "sinkchannels")),
        "channel_layout": get_select_selected(channellayoutselect),
        "delay": get_field_value(dialog, "sinkdelay"),
        "equalizer": sinkinfo.equalizer
    };
    call_api("sinks/"+get_field_value(dialog, "sinkname"), "PUT", JSON.stringify(data), reload_callback);
    dismiss_dialog();
}


function do_update_sink_equalizer() {
    dialog = "update_sink_equalizer"
    var sinkname = get_field_value(dialog, "sinkname")
    equalizer = {
        "b1": (200 - get_field_value(dialog, "eq_b1"))/100,
        "b2": (200 - get_field_value(dialog, "eq_b2"))/100,
        "b3": (200 - get_field_value(dialog, "eq_b3"))/100,
        "b4": (200 - get_field_value(dialog, "eq_b4"))/100,
        "b5": (200 - get_field_value(dialog, "eq_b5"))/100,
        "b6": (200 - get_field_value(dialog, "eq_b6"))/100,
        "b7": (200 - get_field_value(dialog, "eq_b7"))/100,
        "b8": (200 - get_field_value(dialog, "eq_b8"))/100,
        "b9": (200 - get_field_value(dialog, "eq_b9"))/100,
        "b10": (200 - get_field_value(dialog, "eq_b10"))/100,
        "b11": (200 - get_field_value(dialog, "eq_b11"))/100,
        "b12": (200 - get_field_value(dialog, "eq_b12"))/100,
        "b13": (200 - get_field_value(dialog, "eq_b13"))/100,
        "b14": (200 - get_field_value(dialog, "eq_b14"))/100,
        "b15": (200 - get_field_value(dialog, "eq_b15"))/100,
        "b16": (200 - get_field_value(dialog, "eq_b16"))/100,
        "b17": (200 - get_field_value(dialog, "eq_b17"))/100,
        "b18": (200 - get_field_value(dialog, "eq_b18"))/100
    }
    call_api("sinks/" + sinkname + "/equalizer/", "POST", JSON.stringify(equalizer), reload_callback);
}

function do_add_route() {
    dialog = "add_route";
    var sinkselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesink")][0];
    var sourceselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesource")][0];
    var data = {
        "sink": get_select_selected(sinkselect),
        "source": get_select_selected(sourceselect)
    };
    call_api("routes/" + get_field_value(dialog, "routename"), "POST", JSON.stringify(data), reload_callback);
    dismiss_dialog();
}

function do_update_route() {
    dialog = "update_route";
    var sinkselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesink")][0];
    var sourceselect = [... document.querySelectorAll("DIV#" + dialog + " SELECT#routesource")][0];
    var data = {
        "sink": get_select_selected(sinkselect),
        "source": get_select_selected(sourceselect)
    };
    console.log(data);
    call_api("routes/" + get_field_value(dialog, "routename"), "PUT", JSON.stringify(data), reload_callback);
    dismiss_dialog();
}

document.addEventListener("keydown",function(e){
    if(e.key === "Escape") {
        dismiss_dialog();
    }
  });

