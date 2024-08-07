function get_associated_sinks() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    const associatedSinks = sinks.filter(sink =>
        selected_sink.dataset["group_members"].includes(sink.dataset['name']) ||
        sink.dataset["group_members"].includes(selected_sink.dataset['name'])
    );
    return [...associatedSinks.map(sink => sink.dataset['name']), selected_sink?.dataset['name']].filter(Boolean);
}

function get_parent_sinks() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    const parentSinks = sinks.filter(sink =>
        sink.dataset["group_members"].includes(selected_sink.dataset['name'])
    );
    return [...parentSinks.map(sink => sink.dataset['name']), selected_sink?.dataset['name']].filter(Boolean);
}

function get_associated_sources() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    const associatedSources = sources.filter(source =>
        selected_source.dataset["group_members"].includes(source.dataset['name']) ||
        source.dataset["group_members"].includes(selected_source.dataset['name'])
    );
    return [...associatedSources.map(source => source.dataset['name']), selected_source?.dataset['name']].filter(Boolean);
}

function get_parent_sources() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    const parentSources = sources.filter(source =>
        source.dataset["group_members"].includes(selected_source.dataset['name'])
    );
    return [...parentSources.map(source => source.dataset['name']), selected_source?.dataset['name']].filter(Boolean);
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

function highlight_active_routes() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    const associatedSinks = selected_sink ? get_associated_sinks() : [];
    const parentSinks = selected_sink ? get_parent_sinks() : [];
    const associatedSources = selected_source ? get_associated_sources() : [];
    const parentSources = selected_source ? get_parent_sources() : [];

    routes.forEach(route => {
        const isSinkHighlighted = selected_sink && (route.dataset["sink"] === selected_sink.dataset["name"] || parentSinks.includes(route.dataset["sink"]));
        const isSourceHighlighted = selected_source && (route.dataset["source"] === selected_source.dataset["name"] || parentSources.includes(route.dataset["source"]));
        const isSelected = route === selected_route;

        route.classList.toggle("option-selected", isSelected);

        let [r1, g1, b1] = [0, 0, 0];
        let [r2, g2, b2] = [0, 0, 0];

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
            route.style.background = "";
            route.style.backgroundColor = `rgba(${r1}, ${g1}, ${b1}, 0.6)`;
        } else {
            route.style.backgroundColor = "";
            route.style.background = `linear-gradient(90deg, rgba(${r1}, ${g1}, ${b1}, 0.6), rgba(${r2}, ${g2}, ${b2}, 0.6))`;
        }
    });
    drawLines();
}

function highlight_active_sink(color=true) {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));

    sinks.forEach(sink => {
        if (selected_sink) {
            if (sink === selected_sink) {
                sink.classList.add("option-selected");
                if (color)
                    sink.style.backgroundColor = `rgba(${COLORS.sink.r}, ${COLORS.sink.g}, ${COLORS.sink.b}, 0.6)`;
            } else if (selected_sink.dataset["group_members"].includes(sink.dataset['name']) || sink.dataset["group_members"].includes(selected_sink.dataset['name'])) {
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
    highlight_active_routes();
}

function highlight_active_source(color=true) {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));

    sources.forEach(source => {
        if (selected_source) {
            if (source === selected_source) {
                source.classList.add("option-selected");
                if (color)
                    source.style.backgroundColor = `rgba(${COLORS.source.r}, ${COLORS.source.g}, ${COLORS.source.b}, 0.6)`;
            } else if (selected_source.dataset["group_members"].includes(source.dataset['name']) || source.dataset["group_members"].includes(selected_source.dataset['name'])) {
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
    highlight_active_routes();
}

function option_onclick(e) {
    const node = e.target;

    if (node.dataset["type"] === "SourceDescription") {
        selected_source = selected_source === node ? "" : node;
        highlight_active_source();
    } else if (node.dataset["type"] === "SinkDescription") {
        selected_sink = selected_sink === node ? "" : node;
        highlight_active_sink();
    } else if (node.dataset["type"] === "RouteDescription") {
        selected_route = selected_route === node ? "" : node;
        highlight_active_routes();
    }
    start_dummy_audio();
}