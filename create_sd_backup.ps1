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
    Write-Host "File '$ImageFile' already exists. Renaming to '$backupName'..." -ForegroundColor Yellow
    Rename-Item -Path $ImageFile -NewName $backupName
}

# Create image using dd
Write-Host "Creating image with dd..." -ForegroundColor Cyan
# Windows dd uses --progress instead of status=progress
$ddArgs = @("if=$SDDevice", "of=$ImageFile", "bs=4M", "--progress")
# Create temporary files for dd logs
$stdOutLog = [System.IO.Path]::GetTempFileName()
$stdErrLog = [System.IO.Path]::GetTempFileName()

try {
    Write-Host "Running: dd $($ddArgs -join ' ')"
    Write-Host "Logging output to: $stdOutLog and $stdErrLog"
    
    # Run dd with redirection using Start-Process to avoid deadlocks
    $p = Start-Process -FilePath "dd" -ArgumentList $ddArgs -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdOutLog `
        -RedirectStandardError $stdErrLog
    
    if ($p.ExitCode -ne 0) {
        Write-Host "`nWARNING: dd returned non-zero exit code ($($p.ExitCode))." -ForegroundColor Yellow
        Write-Host "This might be normal if it read until the end of the physical drive."
        
        Write-Host "`n=== DD LOG OUTPUT START ===" -ForegroundColor Gray
        if (Test-Path $stdErrLog) { 
            Write-Host "STDERR:" -ForegroundColor Red
            Get-Content $stdErrLog | Write-Host -ForegroundColor Red
        }
        if (Test-Path $stdOutLog) {
            Write-Host "STDOUT:"
            Get-Content $stdOutLog | Write-Host
        }
        Write-Host "=== DD LOG OUTPUT END ===`n" -ForegroundColor Gray
        
        # Heuristic: Check if we have a sizable image file
        if (Test-Path $ImageFile) {
            $createdSize = (Get-Item $ImageFile).Length
            if ($createdSize -gt 10MB) {
                # Arbitrary check that something was written
                Write-Host "Image file exists and has size $([math]::Round($createdSize/1GB, 2)) GB." -ForegroundColor Yellow
                Write-Host "Assuming success despite exit code. Please verify image integrity." -ForegroundColor Yellow
            }
            else {
                Write-Host "CRITICAL: Image file is empty or too small. This was likely a fatal error." -ForegroundColor Red
                exit 1
            }
        }
        else {
            Write-Host "CRITICAL: Image file was not created." -ForegroundColor Red
            exit 1
        }
    }
}
finally {
    if (Test-Path $stdOutLog) { Remove-Item $stdOutLog -ErrorAction SilentlyContinue }
    if (Test-Path $stdErrLog) { Remove-Item $stdErrLog -ErrorAction SilentlyContinue }
}

Write-Host "Image created successfully: $ImageFile" -ForegroundColor Green
$imageSize = (Get-Item $ImageFile).Length
Write-Host "Image size: $([math]::Round($imageSize/1GB, 2)) GB"
exit 0
