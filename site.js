const BASEURL = "/"
var sinks = {}
var sources = {}
var routes = {}

function call_api(endpoint, method, data, callback) {
    const xhr = new XMLHttpRequest();
    xhr.open(method, BASEURL + endpoint, true);
    xhr.getResponseHeader("Content-type", "application/json");
    if (method == "POST")
        xhr.setRequestHeader("Content-Type", "application/json;charset=UTF-8");
        xhr.send(data)

    xhr.onload = function() {
        const obj = JSON.parse(this.responseText);
        if (obj["error"])
            alert(obj["error"])
        else
            callback(obj)
    }
}

function create_option(name, label, id, ip, is_group = false) {
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
        document.getElementById("sources").appendChild(create_option(data[entry].name, data[entry].name + (data[entry].is_group?" [" + data[entry].group_members.join(", ") + "]":"") + (data[entry].enabled?"[Enabled]":"[Disabled]"), entry,null,data[entry].is_group));
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
    [... document.getElementsByTagName("option")].forEach(function(option){option.parentNode.removeChild(option)})
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
    audiotag.src = ""
    audiotag.src = 'http://192.168.3.114:8080/stream/' + sink_ip + '/';
    audiotag.play();
    audiotag.style.display = "inline";
}

function stop_audio() {
    audiotag = document.getElementById("audio")
    audiotag.pause();
    audiotag.style.display = "hidden";
    audio_playing = false
    button = document.getElementById("button_listen").value = "Listen to sink";
}