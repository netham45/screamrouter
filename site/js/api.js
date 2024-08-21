import { selectedRoute, selectedSink, selectedSource, editorActive, editorType, setSelectedSource, setSelectedSink, setSelectedRoute, setEditorActive, setEditorType } from "./global.js";
import {nullCallback, restartCallback, editSourceCallback, editSinkCallback, restartCallback2} from "./main.js"
export function callApi(endpoint, method = "get", data = {}, callback = nullCallback, debug=false) {
    if (debug) {
        console.log("Method:" + method);
        console.log("Endpoint:" + endpoint);
        console.log("Data:" + data);
    }

    data = JSON.stringify(data);
    const xhr = new XMLHttpRequest();
    xhr.open(method, endpoint, true);
    xhr.getResponseHeader("Content-type", "application/json");
    
    if (method.toLowerCase() == "post" || method.toLowerCase() == "put")
        xhr.setRequestHeader("Content-Type", "application/json;charset=UTF-8");

    xhr.send(data)

    xhr.onload = function () {
        try {
            data = JSON.parse(this.responseText);
            let parsedData = data;
            if (data.error != undefined) {
                alert(data.error);
            }
        } catch (error) { }

        callback(this.responseText);
    }
}