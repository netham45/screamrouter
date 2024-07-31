function call_api(endpoint, method, data = {}, callback = null_callback) {
    data = JSON.stringify(data)
    console.log("Calling API " + endpoint + " as " + method + " With data " + data);
    const xhr = new XMLHttpRequest();
    xhr.open(method, endpoint, true);
    xhr.getResponseHeader("Content-type", "application/json");
    
    if (method.toLowerCase() == "post" || method.toLowerCase() == "put")
        xhr.setRequestHeader("Content-Type", "application/json;charset=UTF-8");

    xhr.send(data)

    xhr.onload = function () {
        try {
            data = JSON.parse(this.responseText);
            a = data
            if (data.error != undefined) {
                alert(data.error);
            }
        } catch (error) { }

        callback(this.responseText);
    }
}