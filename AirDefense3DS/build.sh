#!/bin/sh
# Builds AirDefense.3dsx and AirDefense.cia on a Mac or Linux PC with devkitPro installed.
#   1. Install devkitPro pacman: https://devkitpro.org/wiki/Getting_Started
#   2. sudo dkp-pacman -S 3ds-dev
#   3. ./build.sh
set -e
cd "$(dirname "$0")"
if [ -z "$DEVKITPRO" ] && [ -d /opt/devkitpro ]; then
    export DEVKITPRO=/opt/devkitpro
fi
if [ -z "$DEVKITARM" ] && [ -n "$DEVKITPRO" ]; then
    export DEVKITARM="$DEVKITPRO/devkitARM"
fi
if [ ! -f "$DEVKITARM/3ds_rules" ]; then
    echo "devkitARM was not found. Install devkitPro, then run: sudo dkp-pacman -S 3ds-dev"
    exit 1
fi
export PATH="$DEVKITPRO/tools/bin:$PATH"
make
if python3 tools/get_tools.py; then
    make cia
    echo
    echo "Done: AirDefense.cia. Send it to your 3DS with:  python3 tools/send_to_fbi.py <3DS IP address>"
else
    echo
    echo "Built AirDefense.3dsx. The .cia needs makerom and bannertool (see the message above)."
fi
