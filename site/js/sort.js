let new_order_list = [];
let new_order_type = "";
let new_order_pos = 0;

function set_order_callback() {
    new_order_pos++;
    if (new_order_pos == new_order_list.length)
        restart_callback();
    else
        call_api(new_order_type + "s/" + new_order_list[new_order_pos].dataset['name'] + "/reorder/" + new_order_pos, "get", {}, set_order_callback);
}

function set_order(new_order, type) {
    new_order_pos = 0;
    new_order_list = new_order;
    new_order_type = type;
    call_api(new_order_type + "s/" + new_order_list[new_order_pos].dataset['name'] + "/reorder/" + new_order_pos, "get", {}, set_order_callback);
}

function sortByData(property) {
    return function (a,b) {
        return a.dataset[property].localeCompare(b.dataset[property]);
    }   
}

function sort_routes_by_source() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    set_order(routes.sort(sortByData("source")), "route");
}

function sort_routes_by_sink() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    set_order(routes.sort(sortByData("sink")), "route");
}

function sort_routes_by_name() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    set_order(routes.sort(sortByData("name")), "route");
}

function reverse_routes() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    set_order(routes.reverse(), "route");
}

function sort_sinks_by_name() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    set_order(sinks.sort(sortByData("name")), "sink");
}

function reverse_sinks() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    set_order(sinks.reverse(), "sink");
}

function sort_sources_by_name() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    set_order(sources.sort(sortByData("name")), "source");
}

function reverse_sources() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    set_order(sources.reverse(), "source");
}