#!/bin/sh
# Builds and runs the host tests, and builds board_sim (test/host/build/).
set -eu
cd "$(dirname "$0")"
make -s test
make -s board_sim
echo "board_sim: $(pwd)/build/board_sim"
