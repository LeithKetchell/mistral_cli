#!/bin/bash

# BuildMac.sh - Compile mistral_cli for macOS
# Usage: ./BuildMac.sh

# Exit on error
set -e

# Compiler and flags
CXX=clang++
CXXFLAGS="-std=c++17 -Wall -Wextra -O2"
LDFLAGS="-lcurl"
INCLUDES="-I."
SOURCES="mistral_cli.cpp"
OUTPUT="mistral_cli"

# Check for Homebrew-installed dependencies (optional)
if ! command -v brew &> /dev/null; then
    echo "Homebrew not found. Using system libraries."
else
    # Ensure libcurl and nlohmann/json are installed
    if ! brew list curl &> /dev/null; then
        echo "Installing curl via Homebrew..."
        brew install curl
    fi
    LDFLAGS="-L$(brew --prefix curl)/lib $LDFLAGS"
    INCLUDES="-I$(brew --prefix curl)/include $INCLUDES"
fi

# Compile
echo "Building mistral_cli for macOS..."
$CXX $CXXFLAGS $INCLUDES $SOURCES -o $OUTPUT $LDFLAGS

# Verify
if [ -f "$OUTPUT" ]; then
    echo "Build successful: $OUTPUT"
    chmod +x $OUTPUT
else
    echo "Build failed."
    exit 1
fi
