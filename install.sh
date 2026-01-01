#!/bin/bash

# PySer Installation Script for Linux/Mac
# This script performs best-effort checks and prints instructions for missing
# tools. It intentionally does not attempt to install system packages on behalf
# of the user beyond guidance.

set -euo pipefail

echo "Installing PySer (best-effort checks)..."

# Helper to print a message and exit
fail() {
    echo "ERROR: $1" >&2
    exit 1
}

# Check for Python3 and pip
if ! command -v python3 >/dev/null 2>&1; then
    fail "python3 not found. Install Python3 and pip3 (your package manager or from python.org)."
fi
if ! command -v pip3 >/dev/null 2>&1; then
    fail "pip3 not found. Install pip for your Python3 installation."
fi

# Check for git and cmake
if ! command -v git >/dev/null 2>&1; then
    fail "git not found. Please install git."
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "Warning: cmake not found. If you build the native extension you will need cmake." >&2
fi

# vcpkg is optional/advanced; only warn if missing
if ! command -v vcpkg >/dev/null 2>&1; then
    echo "Note: vcpkg not found on PATH. If you rely on vcpkg-managed dependencies, install vcpkg or set VCPKG_ROOT." >&2
fi

# Clone the repository if not already present
if [ ! -d "pyser" ]; then
    echo "Cloning repository..."
    git clone https://github.com/rand0mdevel0per/pyser.git
fi

cd pyser || fail "Failed to enter pyser directory"

# Install Python package (editable install recommended for development)
echo "Installing Python package (editable mode)..."
python3 -m pip install --upgrade pip setuptools wheel
python3 -m setup.py install

cat <<'EOF'
Installation complete.
To build the native extension in-place (if needed):
  mkdir -p build && cd build
  cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
  cmake --build . --config Release
Then copy the produced shared library into the package or run `python -m pip install .` in the project root.
EOF
