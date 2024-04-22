#!/usr/bin/python3
import api
import controller

if __name__ == '__main__':
    controller = controller.Controller()
    api = api.API(controller)
    api.join()