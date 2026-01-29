#!/bin/bash
# Install script for wine-launcher

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Wine Launcher Installation ==="
echo ""

# Check for build dependencies
if ! command -v g++ &> /dev/null; then
    echo "Installing build-essential..."
    sudo apt-get update
    sudo apt-get install -y build-essential
fi

# Check for zenity (for GUI dialogs)
if ! command -v zenity &> /dev/null; then
    echo "Installing zenity for GUI dialogs..."
    sudo apt-get install -y zenity
fi

# Build
echo "Building wine-launcher..."
cd "$SCRIPT_DIR"
make clean
make

# Backup existing wine script if it exists and is not a symlink to us
if [ -f /usr/local/bin/wine ] && [ ! -L /usr/local/bin/wine ]; then
    echo "Backing up existing /usr/local/bin/wine to /usr/local/bin/wine.box86.bak"
    sudo mv /usr/local/bin/wine /usr/local/bin/wine.box86.bak
fi

# Install
echo "Installing wine-launcher..."
sudo make install

# Create symlink as 'wine'
echo "Creating /usr/local/bin/wine symlink..."
sudo ln -sf /usr/local/bin/wine-launcher /usr/local/bin/wine

# Create wine64 symlink too
echo "Creating /usr/local/bin/wine64 symlink..."
sudo ln -sf /usr/local/bin/wine-launcher /usr/local/bin/wine64

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Usage:"
echo "  wine program.exe               # Shows selection dialog"
echo "  wine --backend=auto program.exe       # Auto-detect backend"
echo "  wine --backend=box86 program.exe      # Force Box86"
echo "  wine --backend=box64 program.exe      # Force Box64"
echo "  wine --backend=hangover program.exe   # Force Hangover"
echo "  wine --backend=hangover-fex program.exe  # Hangover with FEX"
echo "  wine --info program.exe        # Show exe info"
echo ""
