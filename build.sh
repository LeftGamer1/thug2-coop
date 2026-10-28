#!/bin/bash
# Builds out/dinput8.dll with MinGW (i686) in Docker (image from tools/Dockerfile).
set -e
cd "$(dirname "$0")"; mkdir -p out
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/w -w /w thug2coop-mingw:1 \
  i686-w64-mingw32-g++-posix -std=c++17 -O2 -Wall -shared -static -static-libgcc -static-libstdc++ \
  -o out/dinput8.dll src/main.cpp src/patch.cpp src/link.cpp src/automation.cpp src/gamepad.cpp src/dinput8.def -lws2_32 -Wl,--enable-stdcall-fixup
echo built out/dinput8.dll
