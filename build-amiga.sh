#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# GFOpenImageWriter (SYS:Tools/ImageWriter) for the 68040, soft float, with the os32-gcc16 stove (GCC 16, as
# GFOpenLayout UI wants). Its window is GFOpenLayout UI in the theme GFOpenPrefs Look chose (GFOpenGadTools'
# lib/ogt_theme.c reads it): OLAYOUT and OGT name the checkouts, else gfopenlayout and gfopengadtools beside
# this repository, else they are fetched (shallow, read only) into build/deps.
#   sh build-amiga.sh
# OUT (default build-amiga) gets Tools/ImageWriter. The Cradle's stove_build sets STOVE and OUT.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
OUT=${OUT:-"$HERE/build-amiga"}
DEPS=${DEPS:-"$HERE/build/deps"}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
need() {    # need repo: beside this checkout, else fetched
    if [ -d "$HERE/../$1" ]; then echo "$HERE/../$1"; return; fi
    if [ ! -d "$DEPS/$1" ]; then
        mkdir -p "$DEPS"
        git clone -q --depth 1 "https://github.com/DalsinAI/$1" "$DEPS/$1" >&2
    fi
    echo "$DEPS/$1"
}
OLAYOUT=${OLAYOUT:-$(need gfopenlayout)}
OGT=${OGT:-$(need gfopengadtools)}
SOFT040="-specs=$HERE/toolchain/m68k-soft-m68040.specs"   # the 68040 soft-float link
FLAGS="-noixemul -m68040 $SOFT040 -std=gnu99 -O2 -fno-delete-null-pointer-checks -fno-common -Wall -Werror \
  -I$HERE -I$OLAYOUT/include -I$OLAYOUT/ui -I$OGT/lib"
# GCC 16.2.0-open3's late-combine pass has a wrong-code bug (GCC PR 123853): off on a compiler that knows it
if "$CC" -fno-late-combine-instructions -S -o /dev/null -x c /dev/null 2>/dev/null; then FLAGS="$FLAGS -fno-late-combine-instructions"; fi
OL="$(echo "$OLAYOUT"/src/*.c) $OLAYOUT/ui/olui.c $OLAYOUT/ui/olui_css.c $OLAYOUT/ui/olui_markup.c $OLAYOUT/ui/olui_theme.c $OLAYOUT/amiga/olui_amiga.c"
mkdir -p "$OUT/Tools"
# shellcheck disable=SC2086
"$CC" $FLAGS -s -o "$OUT/Tools/ImageWriter" "$HERE/imagewriter.c" "$HERE/iw_drive.c" $OL "$OGT/lib/ogt_theme.c"
ls -l "$OUT/Tools/ImageWriter"
