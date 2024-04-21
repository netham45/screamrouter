#!/usr/bin/python3
import api
import controller
import time

if __name__ == '__main__':
    controller = controller.Controller()
    api = api.API(controller)

while True:
    time.sleep(10000)