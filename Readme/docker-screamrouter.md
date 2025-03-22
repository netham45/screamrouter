# ScreamRouter Docker

ScreamRouter can be run in a Docker container, which provides an isolated and consistent environment for the application. This method is recommended for easier setup and management, especially for users who are familiar with Docker.

## Prerequisites

- Docker installed on your system
- Docker Compose installed on your system
- Basic knowledge of Docker commands

## Docker Setup

The ScreamRouter Docker setup consists of:
1. A multi-stage Dockerfile that builds the frontend and C utilities
2. A Docker Compose configuration for easy deployment
3. Helper scripts for building and running the container

## Using the Official Docker Image

The recommended way to run ScreamRouter is to use the official Docker image from Docker Hub:

```bash
docker run -d --network host \
  -v ./config:/app/config \
  -v ./logs:/app/logs \
  -v ./cert:/app/cert \
  --name screamrouter \
  netham45/screamrouter:latest
```

This image is maintained at [https://hub.docker.com/r/netham45/screamrouter](https://hub.docker.com/r/netham45/screamrouter) and contains the latest stable version of ScreamRouter.

## Building from Source (Optional)

If you prefer to build the Docker image yourself, use the provided build script:

```bash
./docker/build.sh
```

This script will build a multi-stage Docker image that:
1. Builds the React frontend
2. Compiles the C utilities
3. Sets up the Python backend with all dependencies

## Running the Docker Container

To run the ScreamRouter Docker container, use the provided run script:

```bash
./docker/run.sh
```

This script will:
1. Create necessary directories for configuration and logs
2. Stop any existing ScreamRouter container
3. Start the container with host networking mode

## Docker Container Details

The ScreamRouter Docker container uses host networking mode to ensure proper functioning of audio streaming protocols like Scream and RTP. This means the container has direct access to the host's network interfaces and ports, avoiding NAT issues that can affect real-time audio streaming.

### Environment Variables

The following environment variables can be used to customize the container:

#### Core Settings
- `TZ`: Set the timezone (default: UTC)
- `API_PORT`: HTTPS port for the web interface (default: 443)
- `HTTP_PORT`: HTTP port for the web interface (default: 80)
- `API_HOST`: Host the web interface binds to (default: 0.0.0.0)

#### Network Ports
- `SCREAM_RECEIVER_PORT`: Port to receive Scream audio data (default: 16401)
- `RTP_RECEIVER_PORT`: Port to receive RTP audio data (default: 40000)
- `SINK_PORT`: Base port for Scream Sink (default: 4010)

For a complete list of environment variables, see the Docker Hub description or refer to the constants module in the source code.

### Persistent Data

The Docker setup maps three important directories to persist data:
- `docker/config`: Contains all configuration files
- `docker/logs`: Contains application logs
- `docker/cert`: Contains SSL certificates

### SSL Certificates

The Docker container automatically generates self-signed SSL certificates on first run if they don't already exist in the mounted certificate directory. These certificates enable HTTPS access to the web interface.

The certificates are stored in the `docker/cert` directory and are preserved between container restarts. If you want to use your own certificates, you can place them in this directory as:
- `server.crt`: The certificate file
- `server.key`: The private key file

### Network Configuration

The container uses host networking mode, which means it shares the host's network namespace. This is required for proper functioning of audio streaming protocols like Scream and RTP that may rely on multicast or specific network configurations.

## Managing the Container

- To view logs: `docker-compose -f docker/docker-compose.yml logs -f`
- To stop the container: `docker-compose -f docker/docker-compose.yml down`
- To restart the container: `docker-compose -f docker/docker-compose.yml restart`

## Accessing ScreamRouter

After the container is running, you can access the ScreamRouter web interface by opening a web browser and navigating to `http://localhost` or the IP address of your server.

## Updating ScreamRouter

To update to the latest version, pull the latest code and rebuild the Docker image:

```bash
git pull
./docker/build.sh
./docker/run.sh
```

This will rebuild the image with the latest code and restart the container.

## Troubleshooting

If you encounter issues with the Docker container, check the logs:

```bash
docker-compose -f docker/docker-compose.yml logs -f
```

For more information on using ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
