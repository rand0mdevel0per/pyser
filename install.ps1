# PySer Installation Script for Windows

Write-Host "Installing PySer..." -ForegroundColor Cyan

# Check if Python is installed
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    # Install Python using Chocolatey
    Write-Host "Python is not installed. Installing Python..." -ForegroundColor Yellow
    # Check if Chocolatey is installed
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Chocolatey is not installed. Installing Chocolatey..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    }
    choco install python -y
}

# Refresh environment variables to recognize Python installation
$env:Path += ";$((Get-Command python).Path | Split-Path -Parent)"

# Verify Python installation
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "Python installation failed. Please install Python manually." -ForegroundColor Red
    exit 1
}

# Check if Git is installed
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    # Install Git using Chocolatey
    Write-Host "Git is not installed. Installing Git..." -ForegroundColor Yellow
    # Check if Chocolatey is installed
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Chocolatey is not installed. Installing Chocolatey..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    }
    choco install git -y
}

# Refresh environment variables to recognize Git installation
$env:Path += ";$((Get-Command git).Path | Split-Path -Parent)"

# Verify Git installation
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "Git installation failed. Please install Git manually." -ForegroundColor Red
    exit 1
}

# Check if vcpkg is installed
if (-not (Test-Path "$env:USERPROFILE\vcpkg")) {
    # Install vcpkg
    Write-Host "vcpkg is not installed. Installing vcpkg..." -ForegroundColor Yellow
    # Check if Chocolatey is installed
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Chocolatey is not installed. Installing Chocolatey..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    }
    choco install vcpkg -y
}

# Refresh environment variables to recognize vcpkg installation
$env:Path += ";$env:USERPROFILE\vcpkg"

# Verify vcpkg installation
if (-not (Get-Command vcpkg -ErrorAction SilentlyContinue)) {
    Write-Host "vcpkg installation failed. Please install vcpkg manually." -ForegroundColor Red
    exit 1
}

# Check if CMake is installed
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    # Install CMake using Chocolatey
    Write-Host "CMake is not installed. Installing CMake..." -ForegroundColor Yellow
    # Check if Chocolatey is installed
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Chocolatey is not installed. Installing Chocolatey..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    }
    choco install cmake -y
}

# Refresh environment variables to recognize CMake installation
$env:Path += ";$((Get-Command cmake).Path | Split-Path -Parent)"

# Verify CMake installation
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "CMake installation failed. Please install CMake manually." -ForegroundColor Red
    exit 1
}

# Check if MSVC Build Tools are installed
if (-not (Get-ChildItem "HKLM:\SOFTWARE\Microsoft\VisualStudio\SxS\VC7" -ErrorAction SilentlyContinue)) {
    # Install MSVC Build Tools using Chocolatey
    Write-Host "MSVC Build Tools are not installed. Installing MSVC Build Tools..." -ForegroundColor Yellow
    # Check if Chocolatey is installed
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Chocolatey is not installed. Installing Chocolatey..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    }
    choco install visualstudio2019buildtools -y --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet"
}

# Refresh environment variables to recognize MSVC installation
$env:Path += ";C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC"

# Verify MSVC Build Tools installation
if (-not (Get-ChildItem "HKLM:\SOFTWARE\Microsoft\VisualStudio\SxS\VC7" -ErrorAction SilentlyContinue)) {
    Write-Host "MSVC Build Tools installation failed. Please install MSVC Build Tools manually." -ForegroundColor Red
    exit 1
}

# Clone the PySer repository
git clone https://github.com/rand0mdevel0per/pyser.git
cd pyser

# Install the package
python setup.py install

Write-Host "PySer installation completed!" -ForegroundColor Green