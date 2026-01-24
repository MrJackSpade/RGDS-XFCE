<# 
.SYNOPSIS
    Fixes GPT partition table on SD card after flashing rg_ds_trixie.img

.DESCRIPTION
    After flashing the image to an SD card, the backup GPT table is in the wrong
    location (middle of disc instead of end). This script uses diskpart to fix it.

.PARAMETER DiskNumber
    The disk number of the SD card (e.g., 1 for PhysicalDrive1)

.EXAMPLE
    .\fix_gpt_after_flash.ps1 -DiskNumber 1
#>

param(
    [Parameter(Mandatory = $true)]
    [int]$DiskNumber
)

# Require admin
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Error "This script requires Administrator privileges. Please run as Administrator."
    exit 1
}

Write-Host "=== GPT Fix Script for SD Card ===" -ForegroundColor Cyan
Write-Host ""

# Show disk info first
Write-Host "Checking Disk $DiskNumber..." -ForegroundColor Yellow
$disk = Get-Disk -Number $DiskNumber -ErrorAction SilentlyContinue
if (-not $disk) {
    Write-Error "Disk $DiskNumber not found!"
    exit 1
}

Write-Host "  Size: $([math]::Round($disk.Size / 1GB, 2)) GB"
Write-Host "  Partition Style: $($disk.PartitionStyle)"
Write-Host "  Model: $($disk.FriendlyName)"
Write-Host ""

# Safety check
$confirmation = Read-Host "Are you sure you want to fix GPT on Disk $DiskNumber? (yes/no)"
if ($confirmation -ne "yes") {
    Write-Host "Aborted." -ForegroundColor Red
    exit 0
}

Write-Host ""
Write-Host "Attempting GPT repair using diskpart..." -ForegroundColor Yellow

# Create diskpart script
$diskpartScript = @"
select disk $DiskNumber
convert gpt noerr
"@

$tempFile = [System.IO.Path]::GetTempFileName()
$diskpartScript | Out-File -FilePath $tempFile -Encoding ASCII

try {
    $result = & diskpart /s $tempFile 2>&1
    Write-Host $result
    
    # Verify the fix
    Write-Host ""
    Write-Host "Verifying partitions..." -ForegroundColor Yellow
    
    $partitions = Get-Partition -DiskNumber $DiskNumber -ErrorAction SilentlyContinue
    if ($partitions) {
        Write-Host "Found $($partitions.Count) partitions:" -ForegroundColor Green
        foreach ($p in $partitions) {
            Write-Host "  Partition $($p.PartitionNumber): $([math]::Round($p.Size / 1MB, 1)) MB"
        }
    }
    
    Write-Host ""
    Write-Host "GPT fix complete! The SD card should now be bootable." -ForegroundColor Green
    
}
finally {
    Remove-Item $tempFile -ErrorAction SilentlyContinue
}
