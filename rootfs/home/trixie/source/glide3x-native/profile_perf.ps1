param(
    [switch]$NoBuild,           # Skip build step (use existing binaries)
    [int]$Timeout = 60,         # Wait time in seconds (default 60)
    [int]$Frequency = 997,      # Sampling frequency in Hz (default 997, prime to avoid aliasing)
    [switch]$KeepData           # Keep perf.data on remote for later analysis
)

$ErrorActionPreference = "Stop"

# Configuration
$RemoteUser = "trixie"
$RemoteHost = "192.168.12.204"
$RemotePath = "/home/trixie/.wine-hangover/drive_c/Program Files (x86)/Diablo II"
$LocalDll = "build/glide3x.dll"
$RemotePerfData = "/tmp/glide3x_perf.data"
$RemoteFlamegraph = "/tmp/glide3x_flamegraph.svg"
$RemoteSymbols = "/tmp/glide3x_symbols.txt"
$LocalProfileDir = "profile_output"
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

Write-Host ""
Write-Host "=== Glide3x Performance Profiler ===" -ForegroundColor Cyan
Write-Host ""

# Create local output directory
if (-not (Test-Path $LocalProfileDir)) {
    New-Item -ItemType Directory -Path $LocalProfileDir -Force | Out-Null
}

if (-not $NoBuild) {
    Write-Host "Building project with debug symbols..."
    wsl make clean all ARCH=i686
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed!"
    }
}

# Extract symbols from DLL locally using nm (via WSL)
Write-Host "Extracting symbols from DLL..."
$LocalSymbols = "${LocalProfileDir}/glide3x_symbols.txt"
# nm outputs: address type name - we want text (T/t) symbols
wsl i686-w64-mingw32-nm build/glide3x.dll 2>$null | Select-String " [Tt] " | Out-File -FilePath $LocalSymbols -Encoding ASCII
$SymbolCount = (Get-Content $LocalSymbols | Measure-Object -Line).Lines
Write-Host "  Extracted $SymbolCount function symbols"

# Deploy DLL and symbols
$SftpScript = @"
cd "${RemotePath}"
put $LocalDll
put $LocalSymbols $RemoteSymbols
bye
"@
Write-Host "Deploying DLL and symbols..."
$SftpScript | & "C:\Windows\System32\OpenSSH\sftp.exe" "${RemoteUser}@${RemoteHost}"

Write-Host ""
Write-Host "=== Deployment complete ===" -ForegroundColor Green
Write-Host ""

# Check if FlameGraph tools exist on remote, install if needed
Write-Host "Checking for FlameGraph tools on remote..."
$CheckFlamegraph = & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "test -f /tmp/FlameGraph/flamegraph.pl && echo 'exists'"
if ($CheckFlamegraph -ne "exists") {
    Write-Host "Installing FlameGraph tools on remote..."
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "cd /tmp && git clone --depth 1 https://github.com/brendangregg/FlameGraph.git 2>/dev/null || true"
}

# Clean up old perf data
Write-Host "Cleaning up old profiling data..."
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -f ${RemotePerfData} ${RemoteFlamegraph} /tmp/perf_*.txt /tmp/perf-*.map"

# Create the perf map generator script on remote
# This will be run after WINE starts to find the DLL base address and generate a proper perf map
$PerfMapScript = @'
#!/bin/bash
# Wait for glide3x.dll to be loaded
SYMBOLS_FILE="$1"
MAX_WAIT=30
WAITED=0

echo "Looking for glide3x.dll in game processes..." >&2

while [ $WAITED -lt $MAX_WAIT ]; do
    # Find processes that might have loaded glide3x.dll
    # Search for Game.exe, Diablo, or wine processes
    for pid in $(pgrep -f 'Game.exe|Diablo|wine' 2>/dev/null); do
        if [ -f "/proc/$pid/maps" ]; then
            # Look for glide3x in the memory map (case insensitive, partial match)
            MAP_LINE=$(grep -i "glide3x" /proc/$pid/maps 2>/dev/null | head -1)
            if [ -n "$MAP_LINE" ]; then
                # Extract base address (first field before the dash)
                BASE_ADDR=$(echo "$MAP_LINE" | cut -d'-' -f1)

                # Handle both 32-bit and 64-bit addresses
                if [ ${#BASE_ADDR} -gt 8 ]; then
                    # 64-bit address - use printf to handle large numbers
                    BASE_DEC=$(printf "%d" "0x$BASE_ADDR" 2>/dev/null || echo "0")
                else
                    BASE_DEC=$((16#$BASE_ADDR))
                fi

                echo "Found glide3x in PID $pid at base 0x$BASE_ADDR" >&2
                echo "Map line: $MAP_LINE" >&2

                # Generate perf map file
                MAPFILE="/tmp/perf-${pid}.map"

                # Read symbols and adjust addresses
                COUNT=0
                while read -r line; do
                    # Parse: <hex_addr> T <symbol_name>
                    ADDR=$(echo "$line" | awk '{print $1}')
                    NAME=$(echo "$line" | awk '{print $3}')
                    if [ -n "$ADDR" ] && [ -n "$NAME" ]; then
                        # Convert to decimal, add base, convert back to hex
                        ADDR_DEC=$((16#$ADDR))
                        FINAL_ADDR=$((BASE_DEC + ADDR_DEC))
                        FINAL_HEX=$(printf "%x" $FINAL_ADDR)
                        # perf map format: <addr> <size> <name>
                        echo "$FINAL_HEX 1 $NAME"
                        COUNT=$((COUNT + 1))
                    fi
                done < "$SYMBOLS_FILE" > "$MAPFILE"

                echo "Generated $MAPFILE with $COUNT symbols" >&2
                echo "$pid"
                exit 0
            fi
        fi
    done
    sleep 1
    WAITED=$((WAITED + 1))
    echo "Waiting... ($WAITED/$MAX_WAIT)" >&2
done

# Debug: show what we can find
echo "=== Debug: game processes ===" >&2
pgrep -af 'Game.exe|Diablo|wine' 2>/dev/null | head -5 >&2
echo "=== Debug: looking for glide in maps ===" >&2
for pid in $(pgrep -f 'Game.exe|Diablo' 2>/dev/null | head -3); do
    echo "PID $pid:" >&2
    grep -i "glide" /proc/$pid/maps 2>/dev/null | head -3 >&2
done

echo "Could not find glide3x.dll - profiling will continue without symbol resolution" >&2
exit 0
'@

# Upload the perf map script
Write-Host "Setting up perf map generator..."
$PerfMapScript | & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "cat > /tmp/gen_perf_map.sh && chmod +x /tmp/gen_perf_map.sh"

# Start the game and perf in background, then generate the map
Write-Host ""
Write-Host "Starting Diablo II with perf profiling..." -ForegroundColor Yellow
Write-Host "  Sampling frequency: ${Frequency} Hz"
Write-Host "  Duration: ${Timeout} seconds"
Write-Host ""

# Command to run everything
# Use single quotes for the here-string to avoid PowerShell variable expansion on $()
$RunCmd = @"
export DISPLAY=:0 && cd '${RemotePath}' && \
(ODLL=libwow64fex.dll WINEPREFIX=~/.wine-hangover /usr/bin/wine 'Diablo II.exe' -3dfx -w 2>&1 &) && \
sleep 3 && \
/tmp/gen_perf_map.sh ${RemoteSymbols} && \
echo 'Attaching perf to system-wide profiling...' && \
perf record -F ${Frequency} --call-graph dwarf -o ${RemotePerfData} -a -- sleep ${Timeout}
"@

$Job = Start-Job -ScriptBlock {
    param($User, $RemoteHostAddr, $Cmd)
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${User}@${RemoteHostAddr}" $Cmd
} -ArgumentList $RemoteUser, $RemoteHost, $RunCmd

Write-Host "Game started. Generating perf map and profiling for $Timeout seconds..."
Write-Host "(Play the game normally to generate representative profile data)" -ForegroundColor Gray

# Wait for the job (which includes the sleep $Timeout)
# Use -ErrorAction to handle stderr from the map generator script
$ErrorActionPreference = "Continue"
$JobResult = Receive-Job -Job $Job -Wait 2>&1
$ErrorActionPreference = "Stop"
if ($JobResult) {
    Write-Host $JobResult
}

Write-Host ""
Write-Host "Stopping Diablo II..."
& "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "pkill -f iabl"

Start-Sleep -Seconds 2
Remove-Job $Job -Force -ErrorAction SilentlyContinue

# Generate flamegraph on remote
Write-Host ""
Write-Host "Generating flamegraph on remote..." -ForegroundColor Yellow

$FlamegraphCmd = @"
cd /tmp && \
perf script -i ${RemotePerfData} 2>/dev/null > /tmp/perf_script.txt && \
./FlameGraph/stackcollapse-perf.pl /tmp/perf_script.txt > /tmp/perf_collapsed.txt 2>/dev/null && \
./FlameGraph/flamegraph.pl --title 'Glide3x - Diablo II (${Timeout}s sample)' /tmp/perf_collapsed.txt > ${RemoteFlamegraph} 2>/dev/null && \
echo 'Flamegraph generated successfully' && \
echo '--- Glide3x functions in profile: ---' && \
grep -E 'gr[A-Z]|voodoo_|display_' /tmp/perf_collapsed.txt | head -20
"@

$Result = & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" $FlamegraphCmd
Write-Host $Result

# Also generate a text report of top functions
Write-Host ""
Write-Host "Generating top functions report..."
$TopCmd = "perf report -i ${RemotePerfData} --stdio --no-children -n --percent-limit 0.5 2>/dev/null | head -80"
$TopFunctions = & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" $TopCmd

# Fetch results
Write-Host ""
Write-Host "Fetching profiling results..."
$LocalFlamegraph = "${LocalProfileDir}/flamegraph_${Timestamp}.svg"
$LocalTopReport = "${LocalProfileDir}/top_functions_${Timestamp}.txt"

$ErrorActionPreference = "Continue"
& "C:\Windows\System32\OpenSSH\scp.exe" "${RemoteUser}@${RemoteHost}:${RemoteFlamegraph}" $LocalFlamegraph 2>$null
$ErrorActionPreference = "Stop"

# Save top functions report
$TopFunctions | Out-File -FilePath $LocalTopReport -Encoding UTF8

# Clean up remote unless KeepData specified
if (-not $KeepData) {
    Write-Host "Cleaning up remote profiling data..."
    & "C:\Windows\System32\OpenSSH\ssh.exe" "${RemoteUser}@${RemoteHost}" "rm -f ${RemotePerfData} /tmp/perf_*.txt /tmp/perf-*.map /tmp/gen_perf_map.sh"
}

# Report results
Write-Host ""
Write-Host "=== Profiling Complete ===" -ForegroundColor Green
Write-Host ""

if (Test-Path $LocalFlamegraph) {
    Write-Host "Flamegraph saved to: $LocalFlamegraph" -ForegroundColor Cyan
    Write-Host "Top functions saved to: $LocalTopReport"
    Write-Host ""
    Write-Host "=== Top Functions (>0.5% samples) ===" -ForegroundColor Yellow
    Write-Host $TopFunctions
    Write-Host ""

    # Open flamegraph in default browser
    Write-Host "Opening flamegraph in browser..." -ForegroundColor Cyan
    Start-Process $LocalFlamegraph
}
else {
    Write-Host "WARNING: Flamegraph generation failed." -ForegroundColor Red
    Write-Host "This may happen if:"
    Write-Host "  - perf wasn't able to record (permissions?)"
    Write-Host "  - The game crashed immediately"
    Write-Host "  - FlameGraph tools failed to install"
    Write-Host ""
    Write-Host "Try running manually on remote:"
    Write-Host "  ssh ${RemoteUser}@${RemoteHost}"
    Write-Host "  perf record -F 99 -g -- wine 'Diablo II.exe' -3dfx -w"
    Write-Host "  perf report"
}

if ($KeepData) {
    Write-Host ""
    Write-Host "Raw perf.data kept at: ${RemotePerfData}" -ForegroundColor Gray
    Write-Host "For interactive analysis: ssh ${RemoteUser}@${RemoteHost} 'perf report -i ${RemotePerfData}'"
}
