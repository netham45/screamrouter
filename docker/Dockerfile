# Stage 1: Build the frontend React application
FROM node:20-slim AS frontend-builder
WORKDIR /app
# Copy the screamrouter-react directory
COPY screamrouter-react/ /app/screamrouter-react/
WORKDIR /app/screamrouter-react
RUN npm install
RUN npm run build
# Now the build output is in /app/site due to webpack.config.js output path

# Stage 2: Build C utilities
FROM debian:bookworm AS c-utils-builder
WORKDIR /app

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    cmake \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libmp3lame-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

# Copy C utils source code
COPY c_utils/ ./c_utils/

# Build C utils
WORKDIR /app/c_utils
RUN ./build.sh

# Stage 3: Final image with Python backend and built components
FROM python:3.11-slim-bookworm

# Set working directory
WORKDIR /app

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libmp3lame0 \
    openssl \
    && rm -rf /var/lib/apt/lists/*

# Create directory for SSL certificates
RUN mkdir -p /app/cert

# Copy Python requirements and install dependencies
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

# Copy entire project
COPY . ./

RUN rm -rf ./screamrouter-react/

# Copy built frontend
COPY --from=frontend-builder /app/site/ ./site/

# Copy built C utils
COPY --from=c-utils-builder /app/c_utils/bin/ ./c_utils/bin/

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