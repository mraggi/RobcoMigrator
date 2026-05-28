#!/bin/bash

# Exit immediately if any sub-script fails
set -e

echo "Starting full build process..."

chmod +x build_on_virtual_machine.sh build_mod.sh

echo -e "\n--- Phase 1: DLL Build (Windows VM) ---"
./build_on_virtual_machine.sh

echo -e "\n--- Phase 2: Papyrus + Packaging ---"
./build_mod.sh

echo -e "\nFull build completed successfully!"
