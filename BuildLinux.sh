#!/bin/bash

# BuildLinux.sh - Compile mistral_cli for Linux
# Usage: ./BuildLinux.sh

# Exit on error
set -e

# Compiler and flags
CXX=g++
CXXFLAGS="-std=c++17 -Wall -Wextra -O2"
LDFLAGS="-lcurl"
INCLUDES="-I."
SOURCES="mistral_cli.cpp"
OUTPUT="mistral_cli"

# Check for pkg-config-known dependencies (optional)
if ! command -v pkg-config &> /dev/null; then
    echo "pkg-config not found. Using system libraries."
else
    # Ensure libcurl is discoverable via pkg-config
    if pkg-config --exists libcurl; then
        LDFLAGS="$(pkg-config --libs libcurl)"
        INCLUDES="$(pkg-config --cflags libcurl) $INCLUDES"
    else
        echo "libcurl not found via pkg-config. Falling back to system libraries."
        echo "If the build fails, install it with: sudo apt install libcurl4-openssl-dev"
    fi
fi

# nlohmann/json is header-only; check it's reachable, but don't fail here —
# the compiler will give a clearer error if it's truly missing.
if [ ! -f /usr/include/nlohmann/json.hpp ] && [ ! -f /usr/local/include/nlohmann/json.hpp ]; then
    echo "Note: nlohmann/json.hpp not found in standard locations."
    echo "If the build fails, install it with: sudo apt install nlohmann-json3-dev"
fi

# Compile
echo "Building mistral_cli for Linux..."
$CXX $CXXFLAGS $INCLUDES $SOURCES -o $OUTPUT $LDFLAGS

# Verify
if [ -f "$OUTPUT" ]; then
    echo "Build successful: $OUTPUT"
    chmod +x $OUTPUT
else
    echo "Build failed."
    exit 1
fi
