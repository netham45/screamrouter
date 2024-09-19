import {drawLines as drawLines} from "./lines.js"
import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
import {startDummyAudio} from "./audio.js";
import { getRouteBySinkSource, exposeFunction } from "./utils.js"

function getAssociatedSinks() {
    if (!selectedSink.dataset["group_members"])
        return [];
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    const associatedSinks = sinks.filter(sink =>
        selectedSink.dataset["group_members"].includes(sink.dataset['name']) ||
        sink.dataset["group_members"].includes(selectedSink.dataset['name'])
    );
    return [...associatedSinks.map(sink => sink.dataset['name']), selectedSink?.dataset['name']].filter(Boolean);
}

function getParentSinks() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    const parentSinks = sinks.filter(sink =>
        sink.dataset["group_members"].includes(selectedSink.dataset['name'])
    );
    return [...parentSinks.map(sink => sink.dataset['name']), selectedSink?.dataset['name']].filter(Boolean);
}

function getAssociatedSources() {
    if (!selectedSource.dataset["group_members"])
        return [];
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    const associatedSources = sources.filter(source =>
        selectedSource.dataset["group_members"].includes(source.dataset['name']) ||
        source.dataset["group_members"].includes(selectedSource.dataset['name'])
    );
    return [...associatedSources.map(source => source.dataset['name']), selectedSource?.dataset['name']].filter(Boolean);
}

function getParentSources() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    const parentSources = sources.filter(source =>
        source.dataset["group_members"].includes(selectedSource.dataset['name'])
    );
    return [...parentSources.map(source => source.dataset['name']), selectedSource?.dataset['name']].filter(Boolean);
}

const COLORS = {
    sink: {
        r: 255,
        g: 192,
        b: 192,
        faded: {
            r: 128,
            g: 96,
            b: 96
        }
    },
    source: {
        r: 128,
        g: 192,
        b: 255,
        faded: {
            r: 64,
            g: 96,
            b: 128
        }
    },
    route: {
        r: 128,
        g: 255,
        b: 128
    }
};

export function highlightActiveRoutes() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    const associatedSinks = selectedSink ? getAssociatedSinks() : [];
    const parentSinks = selectedSink ? getParentSinks() : [];
    const associatedSources = selectedSource ? getAssociatedSources() : [];
    const parentSources = selectedSource ? getParentSources() : [];
    let routeHighlighted = false;
    let routeDoubleHighlighted = false;
    routes.forEach(route => {
        const isSinkHighlighted = selectedSink && (route.dataset["sink"] === selectedSink.dataset["name"] || parentSinks.includes(route.dataset["sink"]));
        const isSourceHighlighted = selectedSource && (route.dataset["source"] === selectedSource.dataset["name"] || parentSources.includes(route.dataset["source"]));
        const isSelected = route === selectedRoute;

        route.classList.toggle("option-selected", isSelected);

        let [r1, g1, b1] = [0, 0, 0];
        let [r2, g2, b2] = [0, 0, 0];
        route.style.display = "none";
        if (isSinkHighlighted && isSourceHighlighted)
            routeDoubleHighlighted = true;
        else if (isSinkHighlighted || isSourceHighlighted)
            routeHighlighted = true;
        if (isSinkHighlighted) {
            if (isSourceHighlighted) {
                [r1, g1, b1] = [COLORS.source.r, COLORS.source.g, COLORS.source.b];
                [r2, g2, b2] = [COLORS.sink.r, COLORS.sink.g, COLORS.sink.b];
                if (isSelected) {
                    g1 += 48;
                    g2 += 48;
                }
            } else {
                if (isSelected) {
                    [r1, g1, b1] = [COLORS.route.r, COLORS.route.g, COLORS.route.b];
                    [r2, g2, b2] = [COLORS.sink.r, COLORS.sink.g, COLORS.sink.b];
                } else {
                    [r1, g1, b1] = [COLORS.sink.r, COLORS.sink.g, COLORS.sink.b];
                }
            }
        } else if (isSourceHighlighted) {
            [r1, g1, b1] = [COLORS.source.r, COLORS.source.g, COLORS.source.b];
            if (isSelected) {
                [r2, g2, b2] = [COLORS.route.r, COLORS.route.g, COLORS.route.b];
            }
        }

        if (isSelected && !(r1 || g1 || b1)) {
            [r1, g1, b1] = [COLORS.route.r, COLORS.route.g, COLORS.route.b];
        }

        if (!(r1 || g1 || b1)) {
            route.style.backgroundColor = "";
            route.style.background = "";
        } else if (!(r2 || g2 || b2)) {
            route.style.display = "block";
            route.style.background = "";
            route.style.backgroundColor = `rgba(${r1}, ${g1}, ${b1}, 0.6)`;
        } else {
            route.style.backgroundColor = "";
            route.style.display = "block";
            route.style.background = `linear-gradient(90deg, rgba(${r1}, ${g1}, ${b1}, 0.6), rgba(${r2}, ${g2}, ${b2}, 0.6))`;
        }
    });
    if (!routeHighlighted) {
        routes.forEach(route => {
            route.style.display = "block";
        });
    }
    if (routeDoubleHighlighted) {
        routes.forEach(route => {
            const isSinkHighlighted = selectedSink && (route.dataset["sink"] === selectedSink.dataset["name"] || parentSinks.includes(route.dataset["sink"]));
            const isSourceHighlighted = selectedSource && (route.dataset["source"] === selectedSource.dataset["name"] || parentSources.includes(route.dataset["source"]));
            const isSelected = route === selectedRoute;

            route.classList.toggle("option-selected", isSelected);

            let [r1, g1, b1] = [0, 0, 0];
            let [r2, g2, b2] = [0, 0, 0];
            route.style.display = "none";
            if (isSinkHighlighted) {
                if (isSourceHighlighted) {
                    [r1, g1, b1] = [COLORS.source.r, COLORS.source.g, COLORS.source.b];
                    [r2, g2, b2] = [COLORS.sink.r, COLORS.sink.g, COLORS.sink.b];
                    if (isSelected) {
                        g1 += 48;
                        g2 += 48;
                    }
                }
            }

            if (isSelected && !(r1 || g1 || b1)) {
                [r1, g1, b1] = [COLORS.route.r, COLORS.route.g, COLORS.route.b];
            }

            if ((r1 || g1 || b1) && (r2 || g2 || b2)) {
                route.style.backgroundColor = "";
                route.style.display = "block";
                route.style.background = `linear-gradient(90deg, rgba(${r1}, ${g1}, ${b1}, 0.6), rgba(${r2}, ${g2}, ${b2}, 0.6))`;
            }
        });
    }
    drawLines();
}

export function highlightActiveSink(color=true) {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));

    sinks.forEach(sink => {
        if (selectedSink) {
            if (sink.dataset['name'] === selectedSink.dataset['name']) {
                sink.classList.add("option-selected");
                if (color)
                    sink.style.backgroundColor = `rgba(${COLORS.sink.r}, ${COLORS.sink.g}, ${COLORS.sink.b}, 0.6)`;
            } else if (selectedSink.dataset["group_members"] && (selectedSink.dataset["group_members"].includes(sink.dataset['name']) || sink.dataset["group_members"].includes(selectedSink.dataset['name']))) {
                sink.classList.remove("option-selected");
                if (color)
                    sink.style.backgroundColor = `rgba(${COLORS.sink.faded.r}, ${COLORS.sink.faded.g}, ${COLORS.sink.faded.b}, 0.6)`;
            } else {
                sink.classList.remove("option-selected");
                if (color)
                    sink.style.backgroundColor = "";
            }
        } else {
            sink.classList.remove("option-selected");
            sink.style.backgroundColor = "";
        }
    });
    updateRouteButtons();
    highlightActiveRoutes();
}

export function highlightActiveSource(color=true) {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));

    sources.forEach(source => {
        if (selectedSource) {
            if (source.dataset['name'] === selectedSource.dataset['name']) {
                source.classList.add("option-selected");
                if (color)
                    source.style.backgroundColor = `rgba(${COLORS.source.r}, ${COLORS.source.g}, ${COLORS.source.b}, 0.6)`;
            } else if (selectedSource.dataset["group_members"].includes(source.dataset['name']) || source.dataset["group_members"].includes(selectedSource.dataset['name'])) {
                source.classList.remove("option-selected");
                if (color)
                    source.style.backgroundColor = `rgba(${COLORS.source.faded.r}, ${COLORS.source.faded.g}, ${COLORS.source.faded.b}, 0.6)`;
            } else {
                source.classList.remove("option-selected");
                if (color)
                    source.style.backgroundColor = "";
            }
        } else {
            source.classList.remove("option-selected");
            if (color)
                source.style.backgroundColor = "";
        }
    });
    updateRouteButtons();
    highlightActiveRoutes();
}

function optionOnclick(e) {
    const node = e.target;

    if (node.dataset["type"] === "SourceDescription") {
        setSelectedSource(selectedSource === node ? "" : node);
        highlightActiveSource();
    } else if (node.dataset["type"] === "SinkDescription") {
        setSelectedSink(selectedSink === node ? "" : node);
        highlightActiveSink();
    } else if (node.dataset["type"] === "RouteDescription") {
        setSelectedRoute(selectedRoute === node ? "" : node);
        highlightActiveRoutes();
    }
    startDummyAudio();
}

export function onload() {
    exposeFunction(optionOnclick, "optionOnclick");
    exposeFunction(highlightActiveSink, "highlightActiveSink");
}