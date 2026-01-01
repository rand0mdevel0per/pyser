#!/bin/bash

# PySer Installation Script for Linux/Mac

echo "Installing PySer..."

# Check for Python3 and pip
if ! command -v python3 &> /dev/null; then
    echo "Python3 not found. Installing Python3..."
elif ! command -v pip3 &> /dev/null; then
    echo "pip3 not found. Installing pip3..."
fi

# Install Python3 and pip if not found
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    if [ -f /etc/debian_version ]; then
        # Ubuntu/Debian
        sudo apt-get update
        sudo apt-get install -y python3 python3-pip git vcpkg cmake build-essential
    elif [ -f /etc/redhat-release ]; then
      # Fedora/RedHat
      sudo dnf install -y python3 python3-pip git vcpkg cmake gcc gcc-c++
    elif [ -f /etc/arch-release ]; then
      # Arch Linux
      sudo pacman -Syu python python-pip git vcpkg cmake gcc --noconfirm
    else
        echo "Unsupported Linux distribution. Please install Python3 and pip manually."
        exit 1
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    # MacOS
    brew update
    brew install python3 git vcpkg cmake gcc
else
    echo "Unsupported OS. Please install Python3 and pip manually."
    exit 1
fi

# Clone the PySer repository
git clone https://github.com/rand0mdevel0per/pyser.git

cd pyser || { echo "Failed to enter pyser directory"; exit 1; }

# Install the package
python3 setup.py install

echo "PySer installation completed!"