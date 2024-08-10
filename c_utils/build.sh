#!/bin/bash
pushd $(dirname $(realpath $0))
make
popd
