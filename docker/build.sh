#!/bin/bash

# Exit on error
set -e

# Change to the docker directory
cd "$(dirname "$0")"

echo "Building ScreamRouter Docker image..."
docker compose build

echo "Build completed successfully."
