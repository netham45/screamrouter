export let selectedSink = "";
export let selectedSource = "";
export let selectedRoute = "";
export let editorActive = false;
export let editorType = "";

export function setSelectedSource(source) {
    selectedSource = source;
}

export function setSelectedSink(sink) {
    selectedSink = sink;
}

export function setSelectedRoute(route) {
    selectedRoute = route;
}

export function setEditorActive(active) {
    editorActive = active;
}

export function setEditorType(type) {
    editorType = type;
}
