#!/bin/bash
mkdir -p cert config logs
if  ! ls cert/{cert,privkey}.pem
then
openssl req -new -newkey rsa:4096 -days 365 -nodes -x509 \
    -subj "/C=US/ST=CO/L=Colorado Springs/O=netham45/CN=screamrouter.netham45.org" \
    -keyout cert/privkey.pem  -out cert/cert.pem
fi

docker container run \
    --network host \
    --restart unless-stopped \
    -d \
    -h "ScreamRouter" \
    -v $PWD/config/:/screamrouter/config/ \
    -v $PWD/logs/:/screamrouter/logs/ \
    -v $PWD/cert/:/root/screamrouter/cert/ \
    screamrouter-docker
