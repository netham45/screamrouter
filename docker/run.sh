#!/bin/bash

# Exit on error
set -e

# Change to the docker directory
cd "$(dirname "$0")"

# Create necessary directories if they don't exist
mkdir -p config
mkdir -p logs
mkdir -p cert

# Check if the container is already running and stop it
if docker ps -q --filter "name=screamrouter" | grep -q .; then
    echo "Stopping existing ScreamRouter container..."
    docker-compose down
fi

echo "Starting ScreamRouter container..."
docker-compose up -d

echo "ScreamRouter is now running in the background."
echo "Access the web interface at http://localhost or the IP address of this machine."
echo ""
echo "To view logs: docker-compose logs -f"
echo "To stop: docker-compose down"