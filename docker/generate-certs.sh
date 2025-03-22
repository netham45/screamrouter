#!/bin/bash

# Define certificate paths
CERT_DIR="/app/cert"
CERT_FILE="$CERT_DIR/server.crt"
KEY_FILE="$CERT_DIR/server.key"

# Check if certificate and key already exist
if [ -f "$CERT_FILE" ] && [ -f "$KEY_FILE" ]; then
    echo "SSL certificates already exist, using existing certificates"
else
    echo "Generating new self-signed SSL certificates..."
    
    # Create certificate directory if it doesn't exist
    mkdir -p "$CERT_DIR"
    
    # Generate a private key
    openssl genrsa -out "$KEY_FILE" 2048
    
    # Generate a self-signed certificate
    openssl req -new -x509 -key "$KEY_FILE" -out "$CERT_FILE" -days 3650 -subj "/C=US/ST=State/L=City/O=Organization/CN=screamrouter.local" -addext "subjectAltName = DNS:screamrouter.local,DNS:localhost,IP:127.0.0.1"
    
    # Set appropriate permissions
    chmod 600 "$KEY_FILE"
    chmod 644 "$CERT_FILE"
    
    echo "SSL certificates generated successfully"
fi

# Output certificate details for logging
echo "Certificate details:"
openssl x509 -in "$CERT_FILE" -noout -text | grep -E "Subject:|Issuer:|Not Before:|Not After :"

# Continue to main application
echo "Starting ScreamRouter..."