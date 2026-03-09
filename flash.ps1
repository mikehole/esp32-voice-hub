<# 
.SYNOPSIS
    Build and flash ESP32 Voice Hub firmware

.DESCRIPTION
    Builds the ESP-IDF firmware and flashes it to the connected ESP32-S3.
    Requires ESP-IDF to be installed (https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html)

.PARAMETER Port
    COM port for the ESP32 (default: auto-detect)

.PARAMETER Monitor
    Open serial monitor after flashing

.PARAMETER BuildOnly
    Only build, don't flash

.PARAMETER Clean
    Remove build directory before building (fixes stale cache issues)

.EXAMPLE
    .\flash.ps1
    .\flash.ps1 -Port COM3
    .\flash.ps1 -Monitor
    .\flash.ps1 -BuildOnly
    .\flash.ps1 -Clean
#>

param(
    [string]$Port = "",
    [switch]$Monitor,
    [switch]$BuildOnly,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Find ESP-IDF
$IdfPath = if ($env:IDF_PATH) { $env:IDF_PATH } else { "$env:USERPROFILE\esp-idf" }

if (-not (Test-Path "$IdfPath\export.ps1")) {
    Write-Host "ESP-IDF not found at $IdfPath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Install ESP-IDF first:" -ForegroundColor Yellow
    Write-Host "  1. Download installer from: https://dl.espressif.com/dl/esp-idf/" -ForegroundColor Cyan
    Write-Host "  2. Or use git:" -ForegroundColor Cyan
    Write-Host "     git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git $env:USERPROFILE\esp-idf" -ForegroundColor Gray
    Write-Host "     cd $env:USERPROFILE\esp-idf" -ForegroundColor Gray
    Write-Host "     .\install.ps1 esp32s3" -ForegroundColor Gray
    exit 1
}

Write-Host "Using ESP-IDF at: $IdfPath" -ForegroundColor Green

# Source ESP-IDF environment
Write-Host "Loading ESP-IDF environment..." -ForegroundColor Cyan
& "$IdfPath\export.ps1"

# Change to firmware directory
$FirmwareDir = Join-Path $PSScriptRoot "firmware\espidf"
if (-not (Test-Path $FirmwareDir)) {
    Write-Host "Firmware directory not found: $FirmwareDir" -ForegroundColor Red
    exit 1
}

Push-Location $FirmwareDir
try {
    # Check for stale build cache (different IDF_PATH)
    $BuildDir = Join-Path $FirmwareDir "build"
    $CacheFile = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $CacheFile) {
        $CacheContent = Get-Content $CacheFile -Raw
        if ($CacheContent -notmatch [regex]::Escape($IdfPath)) {
            Write-Host "Detected stale build cache from different ESP-IDF path" -ForegroundColor Yellow
            $Clean = $true
        }
    }

    # Clean build directory if requested or stale
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "Cleaning build directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
        Write-Host "Build directory removed" -ForegroundColor Green
    }

    # Build
    Write-Host ""
    Write-Host "Building firmware..." -ForegroundColor Cyan
    idf.py build
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }
    Write-Host "Build successful!" -ForegroundColor Green

    if ($BuildOnly) {
        Write-Host ""
        Write-Host "Build complete. Use -Flash to flash the device." -ForegroundColor Yellow
        exit 0
    }

    # Auto-detect COM port if not specified
    if (-not $Port) {
        Write-Host ""
        Write-Host "Detecting COM port..." -ForegroundColor Cyan
        $Ports = Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match "USB|Serial|CP210|CH340|FTDI" }
        if ($Ports) {
            $Port = $Ports[0].DeviceID
            Write-Host "Found: $Port ($($Ports[0].Description))" -ForegroundColor Green
        } else {
            Write-Host "No USB serial port detected. Please specify -Port COMx" -ForegroundColor Red
            Write-Host ""
            Write-Host "Available ports:" -ForegroundColor Yellow
            Get-WmiObject Win32_SerialPort | ForEach-Object { Write-Host "  $($_.DeviceID): $($_.Description)" }
            exit 1
        }
    }

    # Flash
    Write-Host ""
    Write-Host "Flashing to $Port..." -ForegroundColor Cyan
    Write-Host "(This includes bootloader, partition table, firmware, and SR models)" -ForegroundColor Gray
    idf.py -p $Port flash
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Flash failed!" -ForegroundColor Red
        exit 1
    }
    Write-Host "Flash successful!" -ForegroundColor Green

    # Monitor
    if ($Monitor) {
        Write-Host ""
        Write-Host "Opening serial monitor (Ctrl+] to exit)..." -ForegroundColor Cyan
        idf.py -p $Port monitor
    } else {
        Write-Host ""
        Write-Host "Done! Run with -Monitor to see serial output." -ForegroundColor Green
        Write-Host "Or: idf.py -p $Port monitor" -ForegroundColor Gray
    }

} finally {
    Pop-Location
}
