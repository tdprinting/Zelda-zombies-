#!/usr/bin/env bash
# Builds and runs the unit tests for the pure Zombies of Time rules.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-tests
c++ -std=c++20 -Wall -Wextra -Werror -o build-tests/rules_test \
    tests/rules_test.cpp mod/soh/soh/Enhancements/Zombies/ZombiesRules.cpp
./build-tests/rules_test
