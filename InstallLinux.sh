#!/bin/bash

# --- Install mistral_cli to /usr/bin/ ---
# Usage: ./install_mistral_cli.sh

# Check if the script is run as root (required for /usr/bin/)
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)." >&2
    exit 1
fi

# Check if the mistral_cli binary exists in the current directory
if [ ! -f "./mistral_cli" ]; then
    echo "Error: 'mistral_cli' binary not found in the current directory." >&2
    echo "Please ensure you are in the project directory and have built the binary." >&2
    exit 1
fi

# Check if /usr/bin/mistral_cli already exists
if [ -f "/usr/bin/mistral_cli" ]; then
    echo "Warning: '/usr/bin/mistral_cli' already exists."
    read -p "Overwrite? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Installation aborted."
        exit 0
    fi
fi

# Copy the binary to /usr/bin/
echo "Installing mistral_cli to /usr/bin/..."
cp ./mistral_cli /usr/bin/
chmod +x /usr/bin/mistral_cli  # Ensure it's executable

# Verify the installation
if [ -f "/usr/bin/mistral_cli" ]; then
    echo "Success: mistral_cli installed to /usr/bin/."
    echo "You can now run it globally with: mistral_cli"
else
    echo "Error: Failed to install mistral_cli." >&2
    exit 1
fi
