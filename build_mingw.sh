#!/bin/sh
# mingw-w64 でビルド (Linux クロス / MSYS2)
set -e
cd "$(dirname "$0")/src"
CC=${CC:-x86_64-w64-mingw32-gcc}
RC=${RC:-x86_64-w64-mingw32-windres}
$RC north.rc -O coff -o /tmp/north_res.o
$CC -O2 -Wall -municode -mwindows -o ../bin/north.exe main.c ui.c gfx.c net.c util.c login.c stats.c cJSON.c /tmp/north_res.o \
  -lgdiplus -lwinhttp -lcrypt32 -lole32 -loleaut32 -luser32 -lgdi32 -lshell32 -lshlwapi -lmsimg32 -lcomdlg32 -ladvapi32 -static-libgcc
echo "OK -> bin/north.exe"
