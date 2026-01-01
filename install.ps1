# PySer Installation Script for Windows

Write-Host "Installing PySer..." -ForegroundColor Cyan

# Helper: fail with message
function Fail($msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

# Check if Python is installed
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "Python is not installed. Installing Python via Chocolatey..." -ForegroundColor Yellow
    # Install Chocolatey if missing
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Chocolatey is not installed. Installing Chocolatey..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    }
    choco install python -y
}

# Refresh powershell environment (user may need to reopen shell for PATH updates)

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "Git not found. Installing git via Chocolatey..." -ForegroundColor Yellow
    choco install git -y
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "CMake not found. Installing cmake via Chocolatey..." -ForegroundColor Yellow
    choco install cmake -y
}

# vcpkg is optional; inform user how to install
if (-not (Get-Command vcpkg -ErrorAction SilentlyContinue)) {
    Write-Host "Note: vcpkg not found. If you need vcpkg-managed dependencies, see https://github.com/microsoft/vcpkg" -ForegroundColor Yellow
}

# Clone repository if needed
if (-not (Test-Path -Path "./pyser")) {
    git clone https://github.com/rand0mdevel0per/pyser.git
}

Set-Location -Path "./pyser"

# Install the Python package in editable mode for development
python -m pip install --upgrade pip setuptools wheel
python -m setup.py install

Write-Host "Installation complete. To build the native extension, run:" -ForegroundColor Green
Write-Host "  mkdir build; cd build; cmake .. -DCMAKE_TOOLCHAIN_FILE=\"C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake\"; cmake --build . --config Release" -ForegroundColor Green
