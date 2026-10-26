#!/bin/bash
# Build and install into the game: dinput8.dll, coop.ini (if missing), Data\pre\thethug2mod.prx with coop.qb.
# Needs: NS (NeverScript ns binary), ORIG_PRX (THEMOD's original thethug2mod.prx), GAME (the Game folder).
set -e
cd "$(dirname "$0")"
: "${NS:?path to NeverScript ns}" "${ORIG_PRX:?original thethug2mod.prx}" "${GAME:?game folder}"
./build.sh
"$NS" -c qb/coop.ns -o out/coop.qb -targetGame thug2 >/dev/null
python3 -I tools/prx.py add "$ORIG_PRX" out/thethug2mod.prx 'scripts\coop\coop.qb=out/coop.qb' \
  --qdir 'c:\ports\THUG2\THUG2XB\Skate6\data\scripts\coop\coop.qb'
cp out/dinput8.dll "$GAME/dinput8.dll"
cp out/thethug2mod.prx "$GAME/Data/pre/thethug2mod.prx"
[ -f "$GAME/coop.ini" ] || cp coop.ini "$GAME/coop.ini"
echo installed
