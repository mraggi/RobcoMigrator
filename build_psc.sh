#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

failure_alarm() {
    local RED='\033[0;31m'
    local NC='\033[0m'
    echo -e "\n${RED}!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "ERROR: Caprica compilation failed! Script execution halted!"
    echo -e "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!${NC}\n"
}

trap 'failure_alarm' ERR

MOD_NAME="RobCo Migrator"
MOD_DIR="/home/mraggi/Fast/Fluorine/mods/RobCo Migrator"

echo "📜 Compiling Papyrus scripts with Caprica..."

# Staging dirs (also consumed by pack_mod.sh when it zips Data/)
mkdir -p Data/Scripts/Source
mkdir -p "Data/MCM/Config/$MOD_NAME"

cd src
caprica.sh RobCoMigrator.psc
# Caprica bakes the build machine's user/computer name and absolute source path
# into the .pex header; strip them before the pex is copied anywhere.
python3 ../strip_pex_metadata.py RobCoMigrator.pex
cp RobCoMigrator.pex "$MOD_DIR/Scripts/"
cp RobCoMigrator.psc "$MOD_DIR/Scripts/Source/"
mv RobCoMigrator.pex ../Data/Scripts/
cp RobCoMigrator.psc ../Data/Scripts/Source/
cd ..

# MCM configuration
cp src/config.json "Data/MCM/Config/$MOD_NAME/"
cp src/config.json "$MOD_DIR/MCM/Config/RobCo Migrator/"

# Excluded-mods config (shipped default; the DLL also auto-creates it if missing).
mkdir -p Data/F4SE/Plugins
mkdir -p "$MOD_DIR/F4SE/Plugins"
cp src/RobCoMigrator_ExcludedMods.ini Data/F4SE/Plugins/
cp src/RobCoMigrator_ExcludedMods.ini "$MOD_DIR/F4SE/Plugins/"

echo "✅ Caprica compilation complete!"
