#!/bin/bash

# Exit on any error
set -e

# Install dependencies
npm install

# Install copy-webpack-plugin if not already installed
npm install --save-dev copy-webpack-plugin

# Build the project using webpack
npm run build

echo "Build completed. Files are available in the /site directory."
echo "You can now serve the content from the /site directory using a web server."