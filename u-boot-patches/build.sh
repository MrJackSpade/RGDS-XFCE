#!/bin/bash
# U-Boot build script for Anbernic RG-DS
# Run from the u-boot-patches directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UBOOT_DIR="${SCRIPT_DIR}/../u-boot"
RKBIN_DIR="${SCRIPT_DIR}/../rkbin"

# Binaries for RK3568
BL31="${RKBIN_DIR}/bin/rk35/rk3568_bl31_v1.45.elf"
DDR="${RKBIN_DIR}/bin/rk35/rk3568_ddr_1056MHz_v1.23.bin"

# Cross compiler prefix (adjust if needed)
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

echo "=== U-Boot Build for Anbernic RG-DS ==="
echo "U-Boot dir: ${UBOOT_DIR}"
echo "rkbin dir: ${RKBIN_DIR}"
echo "Cross compiler: ${CROSS_COMPILE}gcc"
echo ""

# Check prerequisites
command -v ${CROSS_COMPILE}gcc >/dev/null 2>&1 || { echo "ERROR: Cross compiler not found"; exit 1; }
command -v swig >/dev/null 2>&1 || { echo "ERROR: swig not found. Install with: sudo apt install swig"; exit 1; }
[ -f "${BL31}" ] || { echo "ERROR: BL31 not found at ${BL31}"; exit 1; }
[ -f "${DDR}" ] || { echo "ERROR: DDR blob not found at ${DDR}"; exit 1; }

cd "${UBOOT_DIR}"

# Apply patches if not already applied
if ! grep -q "RGDS" board/anbernic/rgxx3_rk3566/rgxx3-rk3566.c 2>/dev/null; then
    echo "Applying patches..."
    git apply "${SCRIPT_DIR}/0001-board-anbernic-Add-RG-DS-support.patch" || true
    git apply "${SCRIPT_DIR}/0002-configs-Add-RG-DS-to-device-list.patch" || true
fi

# Copy device tree files if not present
if [ ! -f "dts/upstream/src/arm64/rockchip/rk3568-anbernic-rg-ds.dts" ]; then
    echo "Copying device tree files..."
    cp "${SCRIPT_DIR}/rk3568-anbernic-rg-ds.dts" dts/upstream/src/arm64/rockchip/
    cp "${SCRIPT_DIR}/rk3568-anbernic-rg-ds-u-boot.dtsi" arch/arm/dts/
fi

# Clean if requested
if [ "$1" = "clean" ]; then
    echo "Cleaning..."
    make mrproper
    exit 0
fi

# Configure
echo "Configuring..."
make anbernic-rgxx3-rk3566_defconfig

# Build
echo "Building..."
make -j$(nproc) \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    BL31="${BL31}" \
    ROCKCHIP_TPL="${DDR}"

# Check output
if [ -f "u-boot.itb" ] && [ -f "idbloader.img" ]; then
    echo ""
    echo "=== Build Successful ==="
    echo "Output files:"
    ls -la u-boot.itb idbloader.img
    echo ""
    echo "To flash (BACKUP FIRST!):"
    echo "  dd if=idbloader.img of=/dev/mmcblk1 bs=512 seek=64 conv=fsync"
    echo "  dd if=u-boot.itb of=/dev/mmcblk1 bs=512 seek=16384 conv=fsync"
else
    echo "ERROR: Build output not found"
    exit 1
fi
