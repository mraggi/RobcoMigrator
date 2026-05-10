#!/bin/bash

# Exit immediately if any command fails
set -e

MOD_NAME="RobCo Migrator"
ZIP_NAME="${MOD_NAME}.zip"

echo "=== Starting Build & Packaging for $MOD_NAME ==="

# 1. Clean up old build/staging directories
rm -rf Data
rm -f "$ZIP_NAME"

# 2. Create the exact Fallout 4 directory structure
mkdir -p Data/F4SE/Plugins
mkdir -p Data/Scripts/Source/User
mkdir -p "Data/MCM/Config/$MOD_NAME"

# 3. Compile Papyrus and distribute script files
cd src
caprica.sh RobCoMigrator.psc
mv RobCoMigrator.pex ../Data/Scripts/
cp RobCoMigrator.psc ../Data/Scripts/Source/User/
cd ..

# 4. Copy MCM Configuration
cp src/config.json "Data/MCM/Config/$MOD_NAME/"

# 5. Copy the F4SE C++ DLL
if [ -f "build/Release/RobCoMigrator.dll" ]; then
    cp build/Release/RobCoMigrator.dll Data/F4SE/Plugins/
else
    echo "ERROR: DLL not found at build/Release/RobCoMigrator.dll"
    exit 1
fi

# 6. Package into a zip file
cd Data
zip -r "../$ZIP_NAME" *
cd ..

echo "=== Build Complete! ==="
