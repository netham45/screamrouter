# ScreamRouter Docker

There is a basic Docker container to build and run ScreamRouter available.

## Prerequisites

- Docker installed on your system
- Basic knowledge of Docker commands

## Building the Docker Image

The scripts to build the Docker image are in [the docker/ directory](/docker/)

To build the ScreamRouter Docker image, run:

```bash
./build.sh
```

To run the ScreamRouter Docker image, run:

```bash
./run.sh
```

See [ScreamRouter's Configuration](/src/constants/constants.py) for information on some environment variables that can be controlled.
