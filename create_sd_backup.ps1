param(
    [string]$SDDevice = "\\.\PhysicalDrive1",
    [string]$OutputName = "sd_backup"
)

$ErrorActionPreference = "Stop"

# Self-elevation to ensuring admin privileges
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Host "Requesting admin privileges..." -ForegroundColor Yellow
    $newProcess = Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -SDDevice '$SDDevice' -OutputName '$OutputName'" -Wait -PassThru
    exit $newProcess.ExitCode
}

$BackupDir = Join-Path $PSScriptRoot "backups"
if (-not (Test-Path $BackupDir)) {
    New-Item -ItemType Directory -Path $BackupDir | Out-Null
}

$ImageFile = Join-Path $BackupDir "$OutputName.img"

# Function to list available drives
function Show-AvailableDrives {
    Write-Host "`n=== Available Physical Drives ===" -ForegroundColor Cyan
    Get-WmiObject Win32_DiskDrive | ForEach-Object {
        Write-Host "  $($_.DeviceID) - $($_.Model) - $([math]::Round($_.Size/1GB, 2)) GB"
    }
    Write-Host "`n=== Available Volumes ===" -ForegroundColor Cyan
    Get-Volume | Where-Object { $_.DriveLetter } | ForEach-Object {
        Write-Host "  \\.\$($_.DriveLetter): - $($_.FileSystemLabel) - $([math]::Round($_.Size/1GB, 2)) GB"
    }
}

# Check if SD device is specified
if (-not $SDDevice) {
    Write-Host "ERROR: No SD device specified!" -ForegroundColor Red
    Write-Host "`nUsage: .\create_sd_backup.ps1 -SDDevice '\\.\PhysicalDrive2'" -ForegroundColor Yellow
    Show-AvailableDrives
    exit 1
}

Write-Host "=== SD Card Backup ===" -ForegroundColor Green
Write-Host "SD Device: $SDDevice"
Write-Host "Output Image: $ImageFile"
Write-Host ""

# Check if image file exists and rotate if necessary
if (Test-Path $ImageFile) {
    $timestamp = Get-Date -Format "yyyyMMddHHmmss"
    $backupName = "$OutputName.$timestamp.img"
    $backupPath = Join-Path $BackupDir $backupName
    Write-Host "File '$ImageFile' already exists. Renaming to '$backupName'..." -ForegroundColor Yellow
    Rename-Item -Path $ImageFile -NewName $backupName
}

# Create image using dd
Write-Host "Creating image with dd..." -ForegroundColor Cyan
# Windows dd uses --progress instead of status=progress
$ddArgs = @("if=$SDDevice", "of=$ImageFile", "bs=4M", "--progress")
Write-Host "Running: dd $($ddArgs -join ' ')"

# Run dd (assumes dd is in PATH)
$ddProcess = Start-Process -FilePath "dd" -ArgumentList $ddArgs -NoNewWindow -Wait -PassThru

if ($ddProcess.ExitCode -ne 0) {
    Write-Host "ERROR: dd failed with exit code $($ddProcess.ExitCode)" -ForegroundColor Red
    exit 1
}

Write-Host "Image created successfully: $ImageFile" -ForegroundColor Green
$imageSize = (Get-Item $ImageFile).Length
Write-Host "Image size: $([math]::Round($imageSize/1GB, 2)) GB"
