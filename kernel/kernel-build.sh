#!/bin/bash
# Kernel build script for Anbernic RG-DS (RK3568)
# Builds Linux kernel for the device

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="${SCRIPT_DIR}/linux-6.19-rc5"
CONFIG_FILE="${SCRIPT_DIR}/kernel_config"
CROSS_COMPILE="aarch64-linux-gnu-"
ARCH="arm64"
JOBS=$(nproc)

# Device connection info
DEVICE_IP="192.168.12.204"
DEVICE_USER="trixie"
DEVICE_PASS="trixie"

usage() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  config    - Copy config and run olddefconfig"
    echo "  build     - Build kernel and modules"
    echo "  deploy    - Deploy to device via SSH"
    echo "  all       - config + build + deploy"
    echo "  menuconfig - Run menuconfig for configuration"
    echo ""
    echo "Environment:"
    echo "  KERNEL_DIR  - Kernel source directory (default: ${KERNEL_DIR})"
    echo "  DEVICE_IP   - Device IP address (default: ${DEVICE_IP})"
}

check_deps() {
    if ! command -v ${CROSS_COMPILE}gcc &> /dev/null; then
        echo "Error: Cross compiler not found. Install with:"
        echo "  sudo apt install gcc-aarch64-linux-gnu"
        exit 1
    fi
}

do_config() {
    echo "=== Configuring kernel ==="
    if [ ! -d "${KERNEL_DIR}" ]; then
        echo "Error: Kernel source not found at ${KERNEL_DIR}"
        exit 1
    fi

    cp "${CONFIG_FILE}" "${KERNEL_DIR}/.config"
    cd "${KERNEL_DIR}"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} olddefconfig
    echo "Configuration complete"
}

do_menuconfig() {
    echo "=== Running menuconfig ==="
    if [ ! -f "${KERNEL_DIR}/.config" ]; then
        do_config
    fi
    cd "${KERNEL_DIR}"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} menuconfig

    # Save config back to tracked file
    cp "${KERNEL_DIR}/.config" "${CONFIG_FILE}"
    echo "Config saved to ${CONFIG_FILE}"
}

do_build() {
    echo "=== Building kernel ==="
    cd "${KERNEL_DIR}"

    if [ ! -f ".config" ]; then
        echo "No .config found, running config first..."
        do_config
    fi

    # Build kernel image
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j${JOBS} Image.gz

    # Build modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j${JOBS} modules

    # Build device tree
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j${JOBS} dtbs

    echo ""
    echo "=== Build complete ==="
    echo "Kernel: ${KERNEL_DIR}/arch/arm64/boot/Image.gz"
    echo "DTB:    ${KERNEL_DIR}/arch/arm64/boot/dts/rockchip/rk3568-anbernic-rg-ds.dtb"
}

do_deploy() {
    echo "=== Deploying to device ${DEVICE_IP} ==="

    KERNEL_IMAGE="${KERNEL_DIR}/arch/arm64/boot/Image.gz"
    DTB_FILE="${KERNEL_DIR}/arch/arm64/boot/dts/rockchip/rk3568-anbernic-rg-ds.dtb"

    if [ ! -f "${KERNEL_IMAGE}" ]; then
        echo "Error: Kernel image not found. Run build first."
        exit 1
    fi

    # Check if sshpass is available
    if ! command -v sshpass &> /dev/null; then
        echo "Error: sshpass not found. Install with: sudo apt install sshpass"
        exit 1
    fi

    SSH_OPTS="-o StrictHostKeyChecking=no"
    SSH_CMD="sshpass -p '${DEVICE_PASS}' ssh ${SSH_OPTS} ${DEVICE_USER}@${DEVICE_IP}"
    SCP_CMD="sshpass -p '${DEVICE_PASS}' scp ${SSH_OPTS}"

    echo "Copying kernel image..."
    ${SCP_CMD} "${KERNEL_IMAGE}" ${DEVICE_USER}@${DEVICE_IP}:/tmp/

    echo "Copying device tree..."
    ${SCP_CMD} "${DTB_FILE}" ${DEVICE_USER}@${DEVICE_IP}:/tmp/

    echo "Installing on device..."
    ${SSH_CMD} "echo '${DEVICE_PASS}' | sudo -S cp /tmp/Image.gz /boot/"
    ${SSH_CMD} "echo '${DEVICE_PASS}' | sudo -S cp /tmp/rk3568-anbernic-rg-ds.dtb /boot/"

    echo "Installing modules..."
    cd "${KERNEL_DIR}"
    INSTALL_MOD_PATH="/tmp/modules_install"
    rm -rf "${INSTALL_MOD_PATH}"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} INSTALL_MOD_PATH="${INSTALL_MOD_PATH}" modules_install

    # Create tarball and transfer
    MODULES_VERSION=$(ls "${INSTALL_MOD_PATH}/lib/modules/")
    cd "${INSTALL_MOD_PATH}/lib/modules"
    tar czf /tmp/modules.tar.gz "${MODULES_VERSION}"
    ${SCP_CMD} /tmp/modules.tar.gz ${DEVICE_USER}@${DEVICE_IP}:/tmp/

    ${SSH_CMD} "cd /tmp && tar xzf modules.tar.gz && echo '${DEVICE_PASS}' | sudo -S rm -rf /lib/modules/${MODULES_VERSION} && echo '${DEVICE_PASS}' | sudo -S mv ${MODULES_VERSION} /lib/modules/"

    echo "Updating initramfs..."
    ${SSH_CMD} "echo '${DEVICE_PASS}' | sudo -S update-initramfs -u"

    echo ""
    echo "=== Deployment complete ==="
    echo "Reboot the device to use the new kernel"
}

# Main
check_deps

case "${1:-}" in
    config)
        do_config
        ;;
    menuconfig)
        do_menuconfig
        ;;
    build)
        do_build
        ;;
    deploy)
        do_deploy
        ;;
    all)
        do_config
        do_build
        do_deploy
        ;;
    *)
        usage
        ;;
esac
