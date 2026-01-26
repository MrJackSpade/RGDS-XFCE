#!/bin/bash

# Build the project
echo "Building project..."
make || { echo "Build failed"; exit 1; }

# Stop service
echo "Stopping touch-mouse service..."
sudo systemctl stop touch-mouse.service

# Install binary
echo "Installing touch-mouse to /usr/local/bin/..."
sudo cp build/touch-mouse /usr/local/bin/touch-mouse
sudo chmod +x /usr/local/bin/touch-mouse

# Start service
echo "Starting touch-mouse service..."
sudo systemctl start touch-mouse.service

# Wait and check
echo "Waiting 3 seconds for service to stabilize..."
sleep 3

if systemctl is-active --quiet touch-mouse.service; then
    echo "Service is running successfully."
    systemctl status touch-mouse.service --no-pager
else
    echo "ERROR: Service failed to start or stayed inactive."
    systemctl status touch-mouse.service --no-pager
    echo ""
    echo "--- JOURNAL LOGS (Last 50 lines) ---"
    journalctl -u touch-mouse.service -n 50 --no-pager
    exit 1
fi

echo "Installation Complete."
