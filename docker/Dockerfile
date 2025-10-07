FROM python:3.12-trixie

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    libmp3lame0 \
    openssl \
    curl wget \
    && rm -rf /var/lib/apt/lists/*

# Create directory for SSL certificates and config
RUN mkdir -p /app/cert /app/config

# Install Python dependencies and build the project
RUN python -m pip config list || true
RUN env | grep -E '^(PIP|HTTP|HTTPS|NO_PROXY|ALL_PROXY)=' || true
RUN python -m pip index versions screamrouter -v || true
RUN python -m pip install -vvv --no-cache-dir --index-url https://pypi.org/simple screamrouter

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

# Start the application with certificate generation
CMD ["/bin/bash", "-c", "/app/generate-certs.sh && python screamrouter.py"]
