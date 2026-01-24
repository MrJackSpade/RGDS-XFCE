#!/bin/bash
# Script to prepare btrfs filesystem for imaging
# Must be run as root

set -e

echo "=== Filesystem Preparation Script ==="

# 1. Clear temp and apt cache
echo "[1/6] Clearing temp files and apt cache..."
rm -rf /tmp/*
rm -rf /var/tmp/*
apt-get clean
apt-get autoclean

# 2. Defragment btrfs filesystem
echo "[2/6] Defragmenting btrfs filesystem..."
find / -xdev -type f -print0 2>/dev/null | while IFS= read -r -d '' file; do
    btrfs filesystem defragment -v "$file" 2>/dev/null || true
done

# 3. Zero out swap file
echo "[3/6] Zeroing out swap file..."
SWAPFILE=$(swapon --show=NAME --noheadings | head -n1)
if [ -n "$SWAPFILE" ]; then
    echo "Found swap file: $SWAPFILE"
    swapoff "$SWAPFILE"
    SWAP_SIZE=$(stat -c%s "$SWAPFILE")
    rm "$SWAPFILE"
    # Disable compression for the new swap file
    touch "$SWAPFILE"
    chattr +C "$SWAPFILE"
    dd if=/dev/zero of="$SWAPFILE" bs=1M count=$((SWAP_SIZE / 1024 / 1024)) status=progress
    chmod 600 "$SWAPFILE"
    mkswap "$SWAPFILE"
    swapon "$SWAPFILE"
    echo "Swap file recreated with zeros"
else
    echo "No swap file found, skipping..."
fi

# 4. Create zero-fill file to consume free space
echo "[4/6] Creating zero-fill file (disabling btrfs compression)..."
ZEROFILL="/zerofill.tmp"
touch "$ZEROFILL"
# Disable compression on the file so zeros actually take up space
chattr +C "$ZEROFILL"
echo "Filling disk with zeros until full..."
dd if=/dev/zero of="$ZEROFILL" bs=1M status=progress || true
# The dd will fail when disk is full, that's expected
sync

# 5. Delete the zero-fill file
echo "[5/6] Deleting zero-fill file..."
rm -f "$ZEROFILL"
sync

# 6. Shutdown
echo "[6/6] Shutting down..."
sleep 2
shutdown -h now