# SD Card Backup, Sanitize, and Compress Script
# This script:
# 1. Uses dd to create an image of the SD card
# 2. Strips all instances of the string from the image
# 3. Compresses the result with ultra settings

param(
    [string]$SDDevice = "\\.\PhysicalDrive1",
    [string]$OutputName = "sd_backup"
) 

$ErrorActionPreference = "Stop"

# Output paths
$ImageFile = Join-Path $PSScriptRoot "$OutputName.img"
$SanitizedFile = Join-Path $PSScriptRoot "$OutputName_sanitized.img"
$ZipFile = Join-Path $PSScriptRoot "$OutputName.zip"

# Check if SD device is specified
if (-not $SDDevice) {
    Write-Host "ERROR: No SD device specified!" -ForegroundColor Red
    Write-Host "`nUsage: .\backup_sd_sanitized.ps1 -SDDevice '\\.\PhysicalDrive2'" -ForegroundColor Yellow
    exit 1
}

Write-Host "=== SD Card Backup, Sanitize, and Compress ===" -ForegroundColor Green
Write-Host "SD Device: $SDDevice"
Write-Host "Output Image: $ImageFile"

Write-Host ""

# Step 1: Create image using external script
Write-Host "[1/3] Creating image..." -ForegroundColor Cyan

$BackupScript = Join-Path $PSScriptRoot "create_sd_backup.ps1"

if (-not (Test-Path $BackupScript)) {
    Write-Host "ERROR: Required backup script '$BackupScript' not found!" -ForegroundColor Red
    exit 1
}

& $BackupScript -SDDevice $SDDevice -OutputName $OutputName

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Backup script failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

if (-not (Test-Path $ImageFile)) {
    Write-Host "ERROR: Image file expected at '$ImageFile' but was not found after backup." -ForegroundColor Red
    exit 1
}

$imageSize = (Get-Item $ImageFile).Length
Write-Host "Image verified. Size: $([math]::Round($imageSize/1GB, 2)) GB"

# Step 2: Strip password from image (if script exists)
$SanitizeScript = Join-Path $PSScriptRoot "sanitize_sd_image.ps1"

if (Test-Path $SanitizeScript) {
    Write-Host "Found sanitization script. executing..." -ForegroundColor Cyan
    & $SanitizeScript -ImageFile $ImageFile
}
else {
    Write-Host "Sanitization script not found ($SanitizeScript). Skipping password strip." -ForegroundColor Yellow
}

# Step 3: Compress with ultra settings

Write-Host "`n[3/3] Compressing image with ultra settings..." -ForegroundColor Cyan

# Check for 7-Zip
$sevenZip = $null
$sevenZipPaths = @(
    "C:\Program Files\7-Zip\7z.exe",
    "C:\Program Files (x86)\7-Zip\7z.exe",
    (Get-Command 7z -ErrorAction SilentlyContinue).Source
)

foreach ($path in $sevenZipPaths) {
    if ($path -and (Test-Path $path)) {
        $sevenZip = $path
        break
    }
}

if ($sevenZip) {
    Write-Host "Using 7-Zip for compression..."
    # 7-Zip normal compression: -mx=5
    & $sevenZip a -tzip -mx=5 "$ZipFile" "$ImageFile"
    Write-Host "Compressed to: $ZipFile" -ForegroundColor Green
    $compressedSize = (Get-Item $ZipFile).Length
}
else {
    Write-Host "7-Zip not found. Using PowerShell compression (less effective)..." -ForegroundColor Yellow
    Compress-Archive -Path $ImageFile -DestinationPath $ZipFile -CompressionLevel Optimal -Force
    Write-Host "Compressed to: $ZipFile" -ForegroundColor Green
    $compressedSize = (Get-Item $ZipFile).Length
}

$ratio = [math]::Round(($compressedSize / $imageSize) * 100, 1)
Write-Host "Compression ratio: $ratio% ($([math]::Round($compressedSize/1GB, 2)) GB)"

Write-Host "`n=== Complete! ===" -ForegroundColor Green
Write-Host "Original image: $ImageFile"

Write-Host "Compressed file: $ZipFile"
