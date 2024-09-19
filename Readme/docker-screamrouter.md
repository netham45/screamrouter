# ScreamRouter Docker

ScreamRouter can be run in a Docker container, which provides an isolated and consistent environment for the application. This method is recommended for easier setup and management, especially for users who are familiar with Docker.

## Prerequisites

- Docker installed on your system
- Basic knowledge of Docker commands

## Docker Image

The ScreamRouter Docker image is based on Debian and includes all necessary dependencies and components to run ScreamRouter. The Docker container scripts are located in a separate repository: [screamrouter-docker](https://github.com/netham45/screamrouter-docker/).

## Building the Docker Image

To build the ScreamRouter Docker image, navigate to the screamrouter-docker repository and run:

```bash
./build.sh
```

This script will build the Docker image with the latest version of ScreamRouter.

## Running the Docker Container

To run the ScreamRouter Docker container, use:

```bash
./run.sh
```

This script will start the ScreamRouter container, mapping the necessary ports and volumes.

## Accessing ScreamRouter

After the container is running, you can access the ScreamRouter web interface by opening a web browser and navigating to `https://localhost` (or replace `localhost` with your server's IP address if accessing from another device). You may need to accept the self-signed certificate warning.

## Persisting Configuration

The run script maps a volume to persist ScreamRouter's configuration. By default, this is mapped to `./config` in the directory where you run the script. This ensures that your settings are saved even if the container is stopped or restarted.

## Stopping and Restarting the Container

To stop the ScreamRouter container, you can use:

```bash
docker stop screamrouter
```

To restart the container:

```bash
docker start screamrouter
```

Or, you can use the `run.sh` script again, which will stop any existing container and start a new one.

## Updating ScreamRouter

To update to the latest version of ScreamRouter, you'll need to rebuild the Docker image. Stop the current container, then run the build and run scripts again:

```bash
docker stop screamrouter
./build.sh
./run.sh
```

This will create a new image with the latest ScreamRouter code and start a new container.

For more information on using ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
