$ErrorActionPreference = "Stop"

# Configuration
$RemoteUser = "trixie"
$RemoteHost = "192.168.12.204"
$RemotePath = "/home/trixie/.wine-hangover/drive_c/Program Files (x86)/Diablo II"
$LocalDll = "glide3x.dll"
$RemoteLogPath = "/home/trixie/.wine-hangover/drive_c/glide3x_debug.log"
$LocalLogPath = "glide3x_debug.log"

Write-Host "Building project..."
wsl make clean all
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed!"
}

$SftpScript = @"
cd "${RemotePath}"
put $LocalDll
bye
"@
$SftpScript | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}"

Write-Host "Executing Diablo II on remote host..."
# We run this in the background / async or wait? 
# The user wants to wait 20 seconds then kill it.
# If we run 'wine ...' via ssh, it will block until wine exits.
# So we should probably start it in background on remote, or use a timeout here?
# Actually, the user instruction is:
# 2. execute ...
# 3. wait 20 seconds
# 4. pkill
# So we can start it, sleep locally, then open a new ssh to pkill.
# OR we can run it with a timeout command on linux?
# Let's try spawning it in background on remote.

$RunCmd = "export DISPLAY=:0 && cd '${RemotePath}' && ODLL=libwow64fex.dll WINEPREFIX=~/.wine-hangover /usr/bin/wine `'Diablo II.exe`' -3dfx -w 2>&1"
# Use nohup or & to background it so SSH returns? 
# Or just start-job? 
# If we keep SSH open, we can see logs. 
# Let's use PowerShell Start-Job to run the SSH command asynchronously.

$Job = Start-Job -ScriptBlock {
    param($User, $RemoteHostAddr, $Cmd)
    # Bypass wrapper by using absolute path
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${User}@${RemoteHostAddr}" $Cmd
} -ArgumentList $RemoteUser, $RemoteHost, $RunCmd

Write-Host "Game started. Waiting 30 seconds..."
Start-Sleep -Seconds 30

Write-Host "Killing Diablo II..."
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "pkill -f iabl"

# Wait for job to finish (it should finish now that pkill happened)
try {
    Receive-Job -Job $Job -Wait
}
catch {
    Write-Warning "Job reported error (ignoring): $_"
}
Remove-Job $Job

$SftpScriptLog = @"
get "${RemoteLogPath}" $LocalLogPath
bye
"@
$SftpScriptLog | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}"

Write-Host "Done. Log saved to $LocalLogPath"
Get-Content $LocalLogPath -Tail 20
