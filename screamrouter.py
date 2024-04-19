#!/usr/bin/python3
import api
import controller
import uvicorn

if __name__ == '__main__':
    controller = controller.controller()
    api = api(controller)
    uvicorn.run(api.app, port=8080, host='0.0.0.0')