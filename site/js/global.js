import { highlightActiveSink, highlightActiveSource, highlightActiveRoutes } from "./highlighting.js";

export let selectedSink = "";
export let selectedSource = "";
export let selectedRoute = "";
export let editorActive = false;
export let editorType = "";

function setCookie(name, value, days = 7) {
    const expires = new Date(Date.now() + days * 864e5).toUTCString();
    document.cookie = `${name}=${encodeURIComponent(value)}; expires=${expires}; path=/`;
}

function getCookie(name) {
    const cookies = document.cookie.split(';');
    for (let cookie of cookies) {
        const [cookieName, cookieValue] = cookie.split('=').map(c => c.trim());
        if (cookieName === name) {
            return decodeURIComponent(cookieValue);
        }
    }
    return null;
}

export function setSelectedSource(source) {
    selectedSource = source;
    if (source)
        setCookie('selectedSource', source.dataset['name']);
    else
        setCookie('selectedSource', "");
}

export function setSelectedSink(sink) {
    selectedSink = sink;
    if (sink)
        setCookie('selectedSink', sink.dataset['name']);
    else
        setCookie('selectedSink', "");
}

export function setSelectedRoute(route) {
    selectedRoute = route;
    if (route)
        setCookie('selectedRoute', route.dataset['name']);
    else
        setCookie('selectedRoute', "");
}

export function setEditorActive(active) {
    editorActive = active;
}

export function setEditorType(type) {
    editorType = type;
}

export function onload() {
    const selectedSinkName = getCookie('selectedSink');
    if (selectedSinkName) {
        let result = document.querySelector(`SPAN.option[data-name="${selectedSinkName}"]`);
        if (result) {
            selectedSink = result;
        }
    }

    const selectedSourceName = getCookie('selectedSource');
    if (selectedSourceName) {
        let result = document.querySelector(`SPAN.option[data-name="${selectedSourceName}"]`);
        if (result) {
            selectedSource = result;
        }
    }

    const selectedRouteName = getCookie('selectedRoute');
    if (selectedRouteName) {
        let result = document.querySelector(`SPAN.option[data-name="${selectedRouteName}"]`);
        if (result) {
            selectedRoute = result;
        }
    }
}
