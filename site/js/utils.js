export function getRouteBySinkSource(sink, source) {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    let routeIdx = 0;
    for (routeIdx in routes)
        if (routes[routeIdx].dataset['sink'] == sink && routes[routeIdx].dataset['source'] == source)
            return routes[routeIdx];
    return null;
}

export function exposeFunction(func, name) {
    console.log("Exposing function " + name);
    if (window[name] == null)
        window[name] = func;
    else
        console.error("Attempted to expose a function with an already exposed name " + name);
}