#!/usr/bin/env bash
# Copies the Zombies of Time sources into the Shipwright checkout so SoH's CMake
# picks them up (it globs every source file under soh/soh/). Run it again after
# editing anything under mod/.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f Shipwright/CMakeLists.txt ]; then
    git submodule update --init --recursive
fi

cp -R mod/soh/. Shipwright/soh/
echo "Zombies of Time copied into Shipwright/soh/soh/Enhancements/Zombies"
