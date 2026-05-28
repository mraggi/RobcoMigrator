#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

failure_alarm() {
    local RED='\033[0;31m'
    local NC='\033[0m'
    echo -e "\n${RED}!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "ERROR: VM Build failed! Script execution halted!"
    echo -e "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!${NC}\n"
}

trap 'failure_alarm' ERR

# --- CONFIGURATION ---
VM_USER="Builder"
VM_IP="192.168.122.204"
VM_DIR_SCP="C:/actions-runner/_work/RobCoMigrator/RobCoMigrator"
LOCAL_OUT_DIR="./build_output"
# Optional: set to your MO2 mod path to auto-install after building
MOD_DIR="/home/mraggi/Fast/SteamLibrary/steamapps/compatdata/377160/pfx/drive_c/users/steamuser/AppData/Local/ModOrganizer/Fallout 4/mods/RobCo Migrator"

echo "Setting up VM directory..."
ssh $VM_USER@$VM_IP 'if not exist C:\actions-runner\_work\RobCoMigrator\RobCoMigrator mkdir C:\actions-runner\_work\RobCoMigrator\RobCoMigrator'

echo "Syncing source files to Windows VM..."
scp -pr ./src ./CMakeLists.txt ./CMakePresets.json ./vcpkg.json $VM_USER@$VM_IP:"$VM_DIR_SCP/"

# Sync CommonLibF4 only on the first run — it doesn't change between builds
if ! ssh $VM_USER@$VM_IP 'dir C:\actions-runner\_work\RobCoMigrator\RobCoMigrator\lib\CommonLibF4\CMakeLists.txt' >/dev/null 2>&1; then
    echo "Syncing library to VM (first time only)..."
    scp -pr ./lib $VM_USER@$VM_IP:"$VM_DIR_SCP/"
fi

echo "Building on Windows VM..."
if ! ssh $VM_USER@$VM_IP 'set VCPKG_ROOT=C:\vcpkg && cd C:\actions-runner\_work\RobCoMigrator\RobCoMigrator && cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md && cmake --build build --config Release'; then
    echo "Build failed on the VM. Check the logs above."
    exit 1
fi

echo "Fetching the DLL..."
mkdir -p "$LOCAL_OUT_DIR"
mkdir -p build/Release
scp $VM_USER@$VM_IP:"$VM_DIR_SCP/build/Release/RobCoMigrator.dll" "build/Release/RobCoMigrator.dll"
cp "build/Release/RobCoMigrator.dll" "$LOCAL_OUT_DIR/RobCoMigrator.dll"

if [ -n "${MOD_DIR}" ]; then
    mkdir -p "$MOD_DIR/F4SE/Plugins"
    cp "$LOCAL_OUT_DIR/RobCoMigrator.dll" "$MOD_DIR/F4SE/Plugins/"
fi

echo "Build complete! DLL at $LOCAL_OUT_DIR/RobCoMigrator.dll"
