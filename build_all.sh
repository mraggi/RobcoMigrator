#!/bin/bash

# Exit immediately if any sub-script fails
set -e

echo "🚀 Starting full build process..."

# Ensure the sub-scripts are executable
chmod +x build_psc.sh build_on_virtual_machine.sh

echo -e "\n--- Phase 1: Papyrus (Caprica) ---"
./build_psc.sh

echo -e "\n--- Phase 2: DLL Build (Windows VM) ---"
./build_on_virtual_machine.sh

echo -e "\n🏆 Full build completed successfully!"
