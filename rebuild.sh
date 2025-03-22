#!/bin/bash
cd c_utils
./build.sh
cd ..
nohup ./kill_restart.sh restart &