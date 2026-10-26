#!/bin/bash
# Release folder out/release (after install.sh built out/): the files a player copies into the game folder.
set -e; cd "$(dirname "$0")"
rm -rf out/release; mkdir -p out/release/Data/pre
cp out/dinput8.dll coop.ini README_RU.txt README_EN.txt patcher/patch_themod.bat patcher/patch_themod.ps1 out/release/
cp out/thethug2mod.prx out/release/Data/pre/
(cd out && rm -f thug2-coop.zip && python3 -I -c "import shutil; shutil.make_archive('thug2-coop', 'zip', 'release')")
echo out/thug2-coop.zip
