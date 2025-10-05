# Stage 1: Build the frontend React application
FROM node:20-slim AS frontend-builder
WORKDIR /app
# Copy the screamrouter-react directory
COPY screamrouter-react/ /app/screamrouter-react/
WORKDIR /app/screamrouter-react
RUN npm install
RUN npm run build
# Now the build output is in /app/site due to webpack.config.js output path

# Stage 2: Final image with Python backend and built components
FROM python:3.11-slim-bookworm

# Set working directory
WORKDIR /app

# Install build dependencies for setup.py and runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    cmake \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libmp3lame-dev \
    git \
    libmp3lame0 \
    openssl \
    libtool-bin \
    && rm -rf /var/lib/apt/lists/*

# Create directory for SSL certificates and config
RUN mkdir -p /app/cert /app/config

# Copy Python requirements and install dependencies
COPY requirements.txt ./
COPY setup.py ./
COPY pyproject.toml ./
COPY README.md ./
COPY src/ ./src/
COPY build_system/ ./build_system/

# Install Python dependencies and build the project
RUN pip install --no-cache-dir -r requirements.txt .

# Copy the rest of the project (excluding files already copied or built)
# This ensures things like 'docker/', 'images/', 'debian/', 'rpm/' etc. are available if needed at runtime,
# and also brings in the 'site/' directory which should now contain the frontend build.
COPY . ./

# Remove frontend source after build, it's in /app/site now
RUN rm -rf ./screamrouter-react/
# Remove c_utils as it's obsolete
RUN rm -rf ./c_utils/

# Copy built frontend (This step might be redundant if npm run build directly outputs to /app/site and COPY . ./ includes it)
# Verify if /app/site is populated correctly by the npm build and subsequent COPY . ./
# If COPY . ./ already brings the built site, this specific COPY --from can be removed.
# For now, keeping it to ensure frontend is correctly placed.
COPY --from=frontend-builder /app/site/ ./site/

# Clean up build dependencies to reduce image size
RUN apt-get purge -y --auto-remove \
    build-essential \
    g++ \
    cmake \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libmp3lame-dev \
    git \
    && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/*

# Add environment variables
ENV PYTHONUNBUFFERED=1
ENV PYTHONPATH=/app
ENV API_PORT=443
ENV HTTP_PORT=80
ENV CERTIFICATE=/app/cert/cert.pem
ENV CERTIFICATE_KEY=/app/cert/privkey.pem
ENV CONFIG_PATH=/app/config/config.yaml
ENV EQUALIZER_CONFIG_PATH=/app/config/equalizers.yaml

# Copy SSL certificate generation script
COPY docker/generate-certs.sh /app/
RUN chmod +x /app/generate-certs.sh

# Start the application with certificate generation
CMD ["/bin/bash", "-c", "/app/generate-certs.sh && python screamrouter.py"]
