import requests
import json

BASE_URL = "https://screamrouter.netham45.org/"  # Update this if your API is hosted elsewhere

def capture_endpoint(endpoint):
    print(f"Getting {BASE_URL}{endpoint}")
    response = requests.get(f"{BASE_URL}{endpoint}", verify=False)
    if response.status_code == 200:
        return response.json()
    else:
        print(f"Failed to capture {endpoint}: {response.status_code}")
        return None

def main():
    endpoints = [
        "sources",
        "sinks",
        "routes"
    ]

    captured_data = {}

    for endpoint in endpoints:
        data = capture_endpoint(endpoint)
        if data is not None:
            captured_data[endpoint] = data

    with open("api_state.json", "w") as f:
        json.dump(captured_data, f, indent=2)

    print("API state captured and saved to api_state.json")

if __name__ == "__main__":
    main()