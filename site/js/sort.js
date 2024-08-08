import {callApi as callApi} from "./api.js"
import { getRouteBySinkSource, exposeFunction } from "./utils.js"
import {nullCallback, restartCallback, editSourceCallback, editSinkCallback, restartCallback2} from "./main.js"

let newOrderList = [];
let newOrderType = "";
let newOrderPos = 0;

function setOrderCallback() {
    newOrderPos++;
    if (newOrderPos == newOrderList.length)
        restartCallback();
    else
        callApi(newOrderType + "s/" + newOrderList[newOrderPos].dataset['name'] + "/reorder/" + newOrderPos, "get", {}, setOrderCallback);
}

function setOrder(newOrder, type) {
    newOrderPos = 0;
    newOrderList = newOrder;
    newOrderType = type;
    callApi(newOrderType + "s/" + newOrderList[newOrderPos].dataset['name'] + "/reorder/" + newOrderPos, "get", {}, setOrderCallback);
}

function sortByData(property) {
    return function (a,b) {
        return a.dataset[property].localeCompare(b.dataset[property]);
    }   
}

function sortRoutesBySource() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    setOrder(routes.sort(sortByData("source")), "route");
}

function sortRoutesBySink() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    setOrder(routes.sort(sortByData("sink")), "route");
}

function sortRoutesByName() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    setOrder(routes.sort(sortByData("name")), "route");
}

function reverseRoutes() {
    const routes = Array.from(document.querySelectorAll('span[data-type="RouteDescription"]'));
    setOrder(routes.reverse(), "route");
}

function sortSinksByName() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    setOrder(sinks.sort(sortByData("name")), "sink");
}

function reverseSinks() {
    const sinks = Array.from(document.querySelectorAll('span[data-type="SinkDescription"]'));
    setOrder(sinks.reverse(), "sink");
}

function sortSourcesByName() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    setOrder(sources.sort(sortByData("name")), "source");
}

function reverseSources() {
    const sources = Array.from(document.querySelectorAll('span[data-type="SourceDescription"]'));
    setOrder(sources.reverse(), "source");
}

export function onload() {
    exposeFunction(sortRoutesBySource, "sortRoutesBySource");
    exposeFunction(sortRoutesBySink, "sortRoutesBySink");
    exposeFunction(sortRoutesByName, "sortRoutesByName");
    exposeFunction(reverseRoutes, "reverseRoutes");
    exposeFunction(sortSinksByName, "sortSinksByName");
    exposeFunction(reverseSinks, "reverseSinks");
    exposeFunction(sortSourcesByName, "sortSourcesByName");
    exposeFunction(reverseSources, "reverseSources");
}