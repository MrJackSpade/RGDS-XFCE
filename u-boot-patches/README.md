# U-Boot Patches for Anbernic RG-DS

These patches add RG-DS support to mainline U-Boot v2026.01.

## Source
- Device detection based on ADC value 523 (observed from device)
- Device tree decompiled from working /boot/rk3568-anbernic-rg-ds.dtb
- Board code pattern based on existing Anbernic RGxx3 support

## Files in this directory

### Patches (apply with `git apply`)
- `0001-board-anbernic-Add-RG-DS-support.patch` - Adds RGDS to device detection
- `0002-configs-Add-RG-DS-to-device-list.patch` - Adds RG-DS to OF_LIST

### New Files (copy manually)
- `rk3568-anbernic-rg-ds.dts` - Main device tree (copy to `dts/upstream/src/arm64/rockchip/`)
- `rk3568-anbernic-rg-ds-u-boot.dtsi` - U-Boot additions (copy to `arch/arm/dts/`)

### Reference Files
- `rk3568-anbernic-rg-ds.dtb` - Original compiled DTB from device
- `rk3568-anbernic-rg-ds.dts.reference` - ROCKNIX version (for comparison)

## How to Apply

```bash
cd /path/to/u-boot

# Apply patches
git apply ../u-boot-patches/0001-board-anbernic-Add-RG-DS-support.patch
git apply ../u-boot-patches/0002-configs-Add-RG-DS-to-device-list.patch

# Copy new device tree files
cp ../u-boot-patches/rk3568-anbernic-rg-ds.dts dts/upstream/src/arm64/rockchip/
cp ../u-boot-patches/rk3568-anbernic-rg-ds-u-boot.dtsi arch/arm/dts/
```

## Build Instructions

Requires ARM64 cross-compiler and Rockchip rkbin for TPL/BL31.

```bash
# Configure
make anbernic-rgxx3-rk3566_defconfig

# Build (adjust CROSS_COMPILE for your toolchain)
make CROSS_COMPILE=aarch64-linux-gnu- \
     BL31=/path/to/rkbin/bin/rk35/rk3568_bl31_v1.44.elf \
     ROCKCHIP_TPL=/path/to/rkbin/bin/rk35/rk3568_ddr_1056MHz_v1.23.bin

# Output files:
# - u-boot.itb (main U-Boot image)
# - idbloader.img (SPL + TPL)
```

## Flashing

```bash
# Backup existing U-Boot first!
dd if=/dev/mmcblk1 of=uboot-backup.bin bs=512 count=32768

# Flash idbloader at sector 64
dd if=idbloader.img of=/dev/mmcblk1 bs=512 seek=64 conv=fsync

# Flash u-boot.itb at sector 16384
dd if=u-boot.itb of=/dev/mmcblk1 bs=512 seek=16384 conv=fsync
```

## What This Enables

- U-Boot initializes video/display before booting Linux
- Creates early framebuffer for Plymouth boot splash
- Device detection via ADC (value 523)
- Loads correct device tree for RG-DS
