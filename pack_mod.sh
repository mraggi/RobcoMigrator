#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

failure_alarm() {
    local RED='\033[0;31m'
    local NC='\033[0m'
    echo -e "\n${RED}!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "ERROR: A command failed. Script execution halted!"
    echo -e "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!${NC}\n"
}

trap 'failure_alarm' ERR

MOD_NAME="RobCo Migrator"
ZIP_NAME="${MOD_NAME}.zip"

echo "=== Starting Build & Packaging for $MOD_NAME ==="

# 1. Clean up old staging dir + zip
rm -rf Data
rm -f "$ZIP_NAME"

# 2. Build the DLL (VM) and compile Papyrus. build_psc.sh repopulates Data/ with
#    the scripts and MCM config; build_on_virtual_machine.sh builds the DLL.
bash build_all.sh

# 3. Copy the F4SE C++ DLL into staging
mkdir -p Data/F4SE/Plugins
if [ -f "build/Release/RobCoMigrator.dll" ]; then
    cp build/Release/RobCoMigrator.dll Data/F4SE/Plugins/
else
    echo "ERROR: DLL not found at build/Release/RobCoMigrator.dll"
    exit 1
fi

# 4. Package into a zip file
cd Data
zip -r "../$ZIP_NAME" *
cd ..

echo "=== Build Complete! Packaged $ZIP_NAME ==="
