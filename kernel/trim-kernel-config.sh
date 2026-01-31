#!/bin/bash
# Trim kernel config for Anbernic RG-DS
# Removes drivers for hardware not present on the device
#
# Actual hardware on device:
#   - SoC: Rockchip RK3568
#   - CPU: ARM Cortex-A55 (4 cores)
#   - WiFi: Realtek RTL8821CS (SDIO)
#   - Bluetooth: RTL8821CS-BT (UART)
#   - Display: Dual DSI panels (VOP2)
#   - GPU: Mali (Panfrost)
#   - Audio: RK817 codec (I2S)
#   - Touch: Goodix capacitive (x2)
#   - Storage: eMMC + SD card
#   - Input: ADC joystick, ADC keys, GPIO keys, PWM vibrator
#   - USB: EHCI/OHCI host (no devices attached normally)
#   - PMIC: RK805/RK817
#   - NO ethernet port

set -e

CONFIG="${1:-kernel_config}"

if [ ! -f "$CONFIG" ]; then
    echo "Usage: $0 [config_file]"
    echo "Default: kernel_config"
    exit 1
fi

echo "Trimming kernel config: $CONFIG"
cp "$CONFIG" "${CONFIG}.backup"

# Function to disable a config option
disable_config() {
    local opt="$1"
    if grep -q "^${opt}=[ym]" "$CONFIG"; then
        sed -i "s/^${opt}=[ym]/# ${opt} is not set/" "$CONFIG"
        echo "  Disabled: $opt"
    fi
}

# Function to keep only specific options in a group
keep_only() {
    local pattern="$1"
    shift
    local keep=("$@")

    grep -E "^${pattern}=[ym]" "$CONFIG" | while read line; do
        opt="${line%%=*}"
        should_keep=false
        for k in "${keep[@]}"; do
            if [ "$opt" = "$k" ]; then
                should_keep=true
                break
            fi
        done
        if [ "$should_keep" = false ]; then
            disable_config "$opt"
        fi
    done
}

echo ""
echo "=== Removing unused WiFi drivers ==="
# Keep only RTW88 SDIO for RTL8821CS
disable_config "CONFIG_BRCMUTIL"
disable_config "CONFIG_BRCMFMAC"
disable_config "CONFIG_BRCMFMAC_PROTO_BCDC"
disable_config "CONFIG_BRCMFMAC_SDIO"
disable_config "CONFIG_MT7601U"
disable_config "CONFIG_RTL8187"
disable_config "CONFIG_RTL8187_LEDS"
disable_config "CONFIG_RTL8192CU"
disable_config "CONFIG_RTLWIFI"
disable_config "CONFIG_RTLWIFI_USB"
disable_config "CONFIG_RTL8192C_COMMON"
disable_config "CONFIG_RTL8XXXU"
disable_config "CONFIG_RTL8XXXU_UNTESTED"
disable_config "CONFIG_RTL8723BS"
# Disable USB WiFi variants (device uses SDIO only)
disable_config "CONFIG_RTW88_USB"
disable_config "CONFIG_RTW88_8822BU"
disable_config "CONFIG_RTW88_8822CU"
disable_config "CONFIG_RTW88_8723DU"
disable_config "CONFIG_RTW88_8821CU"
# Disable other RTW88 chip variants not on device
disable_config "CONFIG_RTW88_8822B"
disable_config "CONFIG_RTW88_8822BS"
disable_config "CONFIG_RTW88_8822C"
disable_config "CONFIG_RTW88_8822CS"
disable_config "CONFIG_RTW88_8703B"
disable_config "CONFIG_RTW88_8723D"
disable_config "CONFIG_RTW88_8723DS"
disable_config "CONFIG_RTW88_8723CS"
# Keep: RTW88_CORE, RTW88_SDIO, RTW88_8821C, RTW88_8821CS, RTW88_8723X

echo ""
echo "=== Removing unused Bluetooth drivers ==="
disable_config "CONFIG_BT_INTEL"
disable_config "CONFIG_BT_BCM"
disable_config "CONFIG_BT_HCIBTUSB"
disable_config "CONFIG_BT_HCIBTUSB_POLL_SYNC"
disable_config "CONFIG_BT_HCIBTUSB_RTL"
disable_config "CONFIG_BT_HCIUART_BCM"
# Keep: BT_RTL, BT_HCIUART, BT_HCIUART_SERDEV, BT_HCIUART_H4, BT_HCIUART_3WIRE, BT_HCIUART_RTL

echo ""
echo "=== Removing unused USB serial drivers ==="
disable_config "CONFIG_USB_SERIAL_CP210X"
disable_config "CONFIG_USB_SERIAL_FTDI_SIO"
disable_config "CONFIG_USB_SERIAL_KEYSPAN"
disable_config "CONFIG_USB_SERIAL_PL2303"
disable_config "CONFIG_USB_SERIAL_OTI6858"
disable_config "CONFIG_USB_SERIAL_QUALCOMM"
disable_config "CONFIG_USB_SERIAL_SIERRAWIRELESS"
disable_config "CONFIG_USB_SERIAL_OPTION"
disable_config "CONFIG_USB_SERIAL_WWAN"

echo ""
echo "=== Removing unused USB network drivers ==="
disable_config "CONFIG_USB_NET_AX8817X"
disable_config "CONFIG_USB_NET_AX88179_178A"
disable_config "CONFIG_USB_NET_DM9601"
disable_config "CONFIG_USB_NET_SR9700"
disable_config "CONFIG_R8152"

echo ""
echo "=== Removing unused ethernet drivers ==="
disable_config "CONFIG_R8169"
disable_config "CONFIG_R8169_LEDS"

echo ""
echo "=== Removing unused audio codecs ==="
disable_config "CONFIG_SND_SOC_RK3288_HDMI_ANALOG"
disable_config "CONFIG_SND_SOC_RK3399_GRU_SOUND"
disable_config "CONFIG_SND_SOC_AW88395_LIB"
disable_config "CONFIG_SND_SOC_AW87390"
disable_config "CONFIG_SND_SOC_DA7219"
disable_config "CONFIG_SND_SOC_ES8328"
disable_config "CONFIG_SND_SOC_ES8328_I2C"
disable_config "CONFIG_SND_SOC_ES8328_SPI"
disable_config "CONFIG_SND_SOC_MAX98357A"
disable_config "CONFIG_SND_SOC_RT5514"
disable_config "CONFIG_SND_SOC_RT5514_SPI"
# Keep: SND_SOC_RK817, SND_SOC_ROCKCHIP_I2S_TDM, SND_SOC_HDMI_CODEC

echo ""
echo "=== Removing unused touchscreen drivers ==="
disable_config "CONFIG_TOUCHSCREEN_HYNITRON_CSTXXX"
# Keep: TOUCHSCREEN_GOODIX

echo ""
echo "=== Removing unused ethernet PHY drivers ==="
disable_config "CONFIG_REALTEK_PHY"
disable_config "CONFIG_REALTEK_PHY_HWMON"
disable_config "CONFIG_AX88796B_PHY"

echo ""
echo "=== Removing misc unused drivers ==="
# Xbox controller - not used
disable_config "CONFIG_JOYSTICK_XPAD"
disable_config "CONFIG_JOYSTICK_XPAD_FF"
disable_config "CONFIG_JOYSTICK_XPAD_LEDS"

echo ""
echo "=== Config trimming complete ==="
echo "Backup saved to: ${CONFIG}.backup"
echo ""
echo "Run 'diff ${CONFIG}.backup ${CONFIG}' to see changes"
