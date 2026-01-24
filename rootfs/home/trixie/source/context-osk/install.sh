#!/bin/bash
set -e

# Build
make clean
make

# Install Binary
sudo cp context-osk /usr/local/bin/
sudo chmod +x /usr/local/bin/context-osk

# Install Service
mkdir -p ~/.config/systemd/user/
cp context-osk.service ~/.config/systemd/user/
systemctl --user daemon-reload
# Service available but not enabled by default
# systemctl --user enable --now context-osk.service

# Setup Theme Dir
mkdir -p ~/.context-osk/themes/

# Helper for testing PCSX
echo "Creating dummy theme for PCSX..."
PCSX_PATH=$(which pcsx || echo "/usr/local/bin/pcsx")
# Must resolve symlink for theme!
REAL_PCSX=$(readlink -f $PCSX_PATH)
THEME_DIR=~/.context-osk/themes$(dirname $REAL_PCSX)
mkdir -p $THEME_DIR
cat > $THEME_DIR/$(basename $REAL_PCSX).theme <<EOF
[general]
height=300
background_color=#222244

[button]
label=L1
x=10
y=10
width=80
height=40
keycode=10

[button]
label=R1
x=550
y=10
width=80
height=40
keycode=11
EOF

echo "Installation Complete. Context-OSK is running."
