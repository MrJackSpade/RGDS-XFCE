# U-Boot Patches for Anbernic RG-DS

These patches add RG-DS support to mainline U-Boot v2026.01.

## Quick Start

```bash
cd /path/to/boot/u-boot-patches
./build.sh
```

The build script will:
1. Clone U-Boot v2026.01 if not already present
2. Apply the RG-DS patches
3. Build using the included firmware binaries
4. Output `idbloader.img` and `u-boot.itb`

## Directory Structure

```
u-boot-patches/
├── build.sh                 # Main build script
├── bin/                     # Rockchip firmware blobs
│   ├── rk3568_bl31_v1.45.elf
│   └── rk3568_ddr_1056MHz_v1.23.bin
├── 0001-board-anbernic-Add-RG-DS-support.patch
├── 0002-configs-Add-RG-DS-to-device-list.patch
├── rk3568-anbernic-rg-ds.dts
├── rk3568-anbernic-rg-ds-u-boot.dtsi
└── u-boot/                  # (created by build.sh)
```

## Prerequisites

```bash
# Debian/Ubuntu
sudo apt install gcc-aarch64-linux-gnu swig libssl-dev device-tree-compiler

# Arch Linux
sudo pacman -S aarch64-linux-gnu-gcc swig openssl dtc
```

## Source

- Device detection based on ADC value 523 (observed from device)
- Device tree decompiled from working /boot/rk3568-anbernic-rg-ds.dtb
- Board code pattern based on existing Anbernic RGxx3 support

## Firmware Binaries

The `bin/` directory contains Rockchip firmware blobs required for boot:

- `rk3568_bl31_v1.45.elf` - ARM Trusted Firmware (ATF/BL31)
- `rk3568_ddr_1056MHz_v1.23.bin` - DDR initialization (1056MHz = 2112 MT/s)

These are from the official Rockchip rkbin repository.

## Manual Build (if not using build.sh)

```bash
cd u-boot

# Apply patches
git apply ../0001-board-anbernic-Add-RG-DS-support.patch
git apply ../0002-configs-Add-RG-DS-to-device-list.patch

# Copy device tree files
cp ../rk3568-anbernic-rg-ds.dts dts/upstream/src/arm64/rockchip/
cp ../rk3568-anbernic-rg-ds-u-boot.dtsi arch/arm/dts/

# Configure and build
make anbernic-rgxx3-rk3566_defconfig
make CROSS_COMPILE=aarch64-linux-gnu- \
     BL31=../bin/rk3568_bl31_v1.45.elf \
     ROCKCHIP_TPL=../bin/rk3568_ddr_1056MHz_v1.23.bin
```

## Flashing

```bash
# Backup existing U-Boot first!
dd if=/dev/mmcblk1 of=uboot-backup.bin bs=512 count=32768

# Option 1: Flash individual images
dd if=idbloader.img of=/dev/mmcblk1 bs=512 seek=64 conv=fsync
dd if=u-boot.itb of=/dev/mmcblk1 bs=512 seek=16384 conv=fsync

# Option 2: Flash combined image
dd if=u-boot-rockchip.bin of=/dev/mmcblk1 bs=512 seek=64 conv=fsync
```

## What This Enables

- U-Boot initializes video/display before booting Linux
- Creates early framebuffer for Plymouth boot splash
- Device detection via ADC (value 523)
- Loads correct device tree for RG-DS

## Reference Files

- `rk3568-anbernic-rg-ds.dtb` - Original compiled DTB from device
- `rk3568-anbernic-rg-ds.dts.reference` - ROCKNIX version (for comparison)
