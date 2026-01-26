#!/bin/bash
set -e

# Build
make clean
make

# Install Binary
sudo cp context-osk /usr/local/bin/
sudo chmod +x /usr/local/bin/context-osk

# Setup Theme Dir
mkdir -p ~/.context-osk/themes/

echo "Installation Complete."
