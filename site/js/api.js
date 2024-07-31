function call_api(endpoint, method, data = {}, callback = null_callback) {
    console.log(`Calling API ${endpoint} as ${method} with data:`, data);

    return fetch(endpoint, {
        method: method,
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
        .then(response => response.json())
        .then(data => {
            if (data.error) {
                alert(data.error);
            }
            callback(data);
        })
        .catch(error => {
            console.error('Error:', error);
        });
}
