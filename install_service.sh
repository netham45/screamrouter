#!/bin/bash
cp screamrouter.service /etc/systemd/system/screamrouter.service
systemctl daemon-reload
systemctl enable screamrouter.service
systemctl restart screamrouter.service
