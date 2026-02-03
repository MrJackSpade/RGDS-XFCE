param(
    [switch]$TestOnly,      # Only run the LFB stride test, not Diablo 2
    [switch]$DeployOnly,    # Only build and deploy, don't run anything
    [switch]$NoBuild        # Skip build step (use existing binaries)
)

$ErrorActionPreference = "Stop"

# Configuration
$RemoteUser = "trixie"
$RemoteHost = "192.168.12.204"
$RemotePath = "/home/trixie/.wine-hangover/drive_c/Program Files (x86)/Diablo II"
$RemoteTestPath = "/home/trixie/.wine-hangover/drive_c/glide3x_tests"
$LocalDll = "build/glide3x.dll"
$LocalTestGlide = "test_glide.exe"
$LocalTestLfb = "test_lfb_stride.exe"
$RemoteLogPath = "/home/trixie/.wine-hangover/drive_c/glide3x_debug.log"
$LocalLogPath = "glide3x_debug.log"
$RemoteTexturesPath = "/home/trixie/.wine-hangover/drive_c/textures"
$LocalTexturesPath = "textures"

if (-not $NoBuild) {
    Write-Host "Building project (including tests)..."
    wsl make clean all
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed!"
    }
}

# Create test directory on remote if it doesn't exist
Write-Host "Creating test directory on remote..."
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "mkdir -p '${RemoteTestPath}'"

# Deploy DLL and test executables
$SftpScript = @"
cd "${RemotePath}"
put $LocalDll
cd "${RemoteTestPath}"
put $LocalDll
put $LocalTestGlide
put $LocalTestLfb
bye
"@
Write-Host "Deploying DLL and test executables..."
$SftpScript | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}"

Write-Host ""
Write-Host "=== Deployment complete ==="
Write-Host "DLL deployed to: ${RemotePath}"
Write-Host "Tests deployed to: ${RemoteTestPath}"
Write-Host ""

if ($DeployOnly) {
    Write-Host "Deploy-only mode. To run tests manually:"
    Write-Host "  ssh ${RemoteUser}@${RemoteHost}"
    Write-Host "  cd '${RemoteTestPath}'"
    Write-Host "  DISPLAY=:0 WINEPREFIX=~/.wine-hangover wine test_lfb_stride.exe"
    exit 0
}

if ($TestOnly) {
    Write-Host "=== Running LFB Stride Test (ZARDBLIZ bug reproduction) ==="
    Write-Host ""

    # Clear any old logs
    if (Test-Path $LocalLogPath) { Remove-Item $LocalLogPath }
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -f '${RemoteLogPath}'"

    $TestCmd = "export DISPLAY=:0 && cd '${RemoteTestPath}' && WINEPREFIX=~/.wine-hangover /usr/bin/wine test_lfb_stride.exe 2>&1"

    Write-Host "Executing test..."
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" $TestCmd

    Write-Host ""
    Write-Host "Fetching debug log..."
    $SftpScriptLog = @"
get "${RemoteLogPath}" $LocalLogPath
bye
"@
    $SftpScriptLog | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}"

    Write-Host ""
    Write-Host "=== Debug Log (last 50 lines) ==="
    Get-Content $LocalLogPath -Tail 50
    exit 0
}

# Default: Run Diablo II
Write-Host "Executing Diablo II on remote host..."

# Clear any old logs
if (Test-Path $LocalLogPath) { Remove-Item $LocalLogPath }
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -f '${RemoteLogPath}'"

# Clear textures directories (local and remote)
Write-Host "Clearing textures directories..."
if (Test-Path $LocalTexturesPath) { Remove-Item $LocalTexturesPath -Recurse -Force }
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -rf '${RemoteTexturesPath}'"

$RunCmd = "export DISPLAY=:0 && cd '${RemotePath}' && ODLL=libwow64fex.dll WINEPREFIX=~/.wine-hangover /usr/bin/wine `'Diablo II.exe`' -3dfx -w 2>&1"

$Job = Start-Job -ScriptBlock {
    param($User, $RemoteHostAddr, $Cmd)
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${User}@${RemoteHostAddr}" $Cmd
} -ArgumentList $RemoteUser, $RemoteHost, $RunCmd

Write-Host "Game started. Waiting 30 seconds..."
Start-Sleep -Seconds 90

Write-Host "Killing Diablo II..."
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "pkill -f iabl"

# Wait for job to finish
try {
    Receive-Job -Job $Job -Wait
}
catch {
    Write-Warning "Job reported error (ignoring): $_"
}
Remove-Job $Job

# Fetch debug log
$SftpScriptLog = @"
get "${RemoteLogPath}" $LocalLogPath
bye
"@
$SftpScriptLog | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}"

# Fetch textures directory
Write-Host "Fetching textures directory..."
& "C:\Windows\System32\OpenSSH\scp.exe" -r "${RemoteUser}@${RemoteHost}:${RemoteTexturesPath}" $LocalTexturesPath

$TextureCount = 0
if (Test-Path $LocalTexturesPath) {
    $TextureCount = (Get-ChildItem $LocalTexturesPath -Filter "*.bmp" | Measure-Object).Count
}

Write-Host ""
Write-Host "Done. Log saved to $LocalLogPath"
Write-Host "Textures saved to $LocalTexturesPath ($TextureCount BMP files)"
Write-Host ""
Write-Host "=== Debug Log (last 20 lines) ==="
Get-Content $LocalLogPath -Tail 20
