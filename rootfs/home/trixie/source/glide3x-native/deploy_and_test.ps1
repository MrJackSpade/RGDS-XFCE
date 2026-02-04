param(
    [string]$Test,          # Test to run: "lfb", "texture", "texmem", "glide" (or empty for Diablo 2)
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
$LocalTestTexture = "test_texture.exe"
$LocalTestTexMem = "test_texture_memory.exe"
$LocalTestTexSimple = "test_texture_simple.exe"
$RemoteLogPath = "/home/trixie/.wine-hangover/drive_c/glide3x_debug.log"
$LocalLogPath = "glide3x_debug.log"
$RemoteTexturesPath = "/home/trixie/.wine-hangover/drive_c/textures"
$LocalTexturesPath = "textures"
$RemoteTmuDumpPath = "/home/trixie/.wine-hangover/drive_c"
$LocalDiagnosticsPath = "diagnostics"

# Clear textures directories (local and remote)
Write-Host "Clearing textures directories..."
if (Test-Path $LocalTexturesPath) { Remove-Item $LocalTexturesPath -Recurse -Force }
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -rf '${RemoteTexturesPath}'"

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
put $LocalTestTexture
put $LocalTestTexMem
put $LocalTestTexSimple
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
    Write-Host "  DISPLAY=:0 WINEPREFIX=~/.wine-hangover wine test_texture.exe"
    Write-Host ""
    Write-Host "Available tests: -Test lfb, -Test texture, -Test texmem, -Test glide"
    exit 0
}

# Run a specific test if requested
if ($Test) {
    $TestExe = switch ($Test.ToLower()) {
        "lfb" { "test_lfb_stride.exe" }
        "texture" { "test_texture.exe" }
        "texmem" { "test_texture_memory.exe" }
        "texsimple" { "test_texture_simple.exe" }
        "glide" { "test_glide.exe" }
        default {
            Write-Error "Unknown test: $Test. Available: lfb, texture, texmem, texsimple, glide"
            exit 1
        }
    }

    Write-Host "=== Running Test: $Test ($TestExe) ==="
    Write-Host ""

    # Clear any old logs and dumps
    if (Test-Path $LocalLogPath) { Remove-Item $LocalLogPath }
    if (Test-Path $LocalDiagnosticsPath) { Remove-Item $LocalDiagnosticsPath -Recurse -Force }
    New-Item -ItemType Directory -Path $LocalDiagnosticsPath -Force | Out-Null
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -f '${RemoteLogPath}' '${RemoteTmuDumpPath}/tmu0_dump.bin' '${RemoteTmuDumpPath}/tmu0_full_dump.bin'"

    $TestCmd = "export DISPLAY=:0 && cd '${RemoteTestPath}' && WINEPREFIX=~/.wine-hangover /usr/bin/wine $TestExe 2>&1"

    Write-Host "Executing test (interactive tests require input on remote display)..."
    & "C:\Windows\System32\OpenSSH\ssh.exe" -t "${RemoteUser}@${RemoteHost}" $TestCmd
    $TestExitCode = $LASTEXITCODE

    Write-Host ""
    Write-Host "Fetching debug log and diagnostics..."
    $SftpScriptLog = @"
get "${RemoteLogPath}" $LocalLogPath
get "${RemoteTmuDumpPath}/tmu0_dump.bin" "${LocalDiagnosticsPath}/tmu0_dump.bin"
get "${RemoteTmuDumpPath}/tmu0_full_dump.bin" "${LocalDiagnosticsPath}/tmu0_full_dump.bin"
bye
"@
    $ErrorActionPreference = "Continue"
    $SftpScriptLog | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}" 2>$null
    $ErrorActionPreference = "Stop"

    Write-Host ""
    Write-Host "=== Debug Log (last 50 lines) ==="
    if (Test-Path $LocalLogPath) {
        Get-Content $LocalLogPath -Tail 50
    }
    else {
        Write-Host "(No log file found)"
    }

    # Report diagnostics
    Write-Host ""
    Write-Host "=== Diagnostics ==="
    $DiagFiles = Get-ChildItem $LocalDiagnosticsPath -ErrorAction SilentlyContinue
    if ($DiagFiles) {
        foreach ($f in $DiagFiles) {
            Write-Host "  $($f.Name) - $($f.Length) bytes"
        }
        Write-Host ""
        Write-Host "TMU dumps saved to: $LocalDiagnosticsPath"
        Write-Host "Use hex editor to examine TMU memory contents"
    }
    else {
        Write-Host "(No diagnostic dumps generated)"
    }
    exit $TestExitCode
}

# Default: Run Diablo II
Write-Host "Executing Diablo II on remote host..."

# Clear any old logs
if (Test-Path $LocalLogPath) { Remove-Item $LocalLogPath }
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -f '${RemoteLogPath}'"



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
