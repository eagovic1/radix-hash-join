#!/usr/bin/env bash
# Prevodi sest verzija hash joina GCC-om u sest .exe fajlova u bin/gcc/
# (MSVC verzije iz build.bat ostaju u bin/, pa se mogu porediti).
#
# Pokretanje iz terminala "MSYS2 UCRT64" (treba: pacman -S mingw-w64-ucrt-x86_64-gcc):
#   cd /c/dev/radix-hash-join
#   ./build_gcc.sh
#
# Ako nadje VTune SDK, prevede ittnotify iz sdk/src GCC-om (MSVC-ov
# libittnotify.lib se ne moze linkovati), pa VTune mjeri samo join.
# Drugi SDK:  VTUNE_SDK="/c/neka/putanja/sdk" ./build_gcc.sh
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v g++ >/dev/null 2>&1; then
    echo "GRESKA: g++ nije nadjen. Pokreni iz MSYS2 UCRT64 terminala i instaliraj:"
    echo "  pacman -S mingw-w64-ucrt-x86_64-gcc"
    exit 1
fi

OUT=bin/gcc
mkdir -p "$OUT/obj"

# -O2 kao /O2 kod MSVC-a; -g za simbole; -static da exe ne trazi
# libstdc++/libgcc DLL-ove kad ga VTune pokrene izvan MSYS2 terminala.
FLAGS=(-O2 -g -fno-omit-frame-pointer -std=c++17 -Wall)
LDFLAGS=(-static)

SDK="${VTUNE_SDK:-/c/Program Files (x86)/Intel/oneAPI/vtune/latest/sdk}"
ITT_FLAGS=()
ITT_OBJ=()
if [[ -f "$SDK/include/ittnotify.h" && -f "$SDK/src/ittnotify/ittnotify_static.c" ]]; then
    if gcc -O2 -c "$SDK/src/ittnotify/ittnotify_static.c" \
           -I"$SDK/include" -I"$SDK/src/ittnotify" \
           -o "$OUT/obj/ittnotify_static.o" 2>"$OUT/obj/ittnotify.log"; then
        ITT_FLAGS=(-DKORISTI_ITT -I"$SDK/include")
        ITT_OBJ=("$OUT/obj/ittnotify_static.o")
        echo "VTune ITT API preveden GCC-om: profilira se samo join."
    else
        echo "UPOZORENJE: ittnotify se nije preveo GCC-om (vidi $OUT/obj/ittnotify.log),"
        echo "            profilira se cijeli program."
    fi
else
    echo "VTune ITT API nije nadjen: profilira se cijeli program."
fi

prevedi() {
    local broj=$1 ime=$2
    echo "Prevodim $ime.exe ..."
    # -fopt-info-vec-all: izvjestaj koje petlje su vektorizovane i zasto nisu
    if ! g++ "${FLAGS[@]}" "${ITT_FLAGS[@]}" -DVARIJANTA="$broj" vtune_main.cpp \
            -fopt-info-vec-all="$OUT/obj/$ime.vec.txt" \
            "${ITT_OBJ[@]}" "${LDFLAGS[@]}" -o "$OUT/$ime.exe" >"$OUT/obj/$ime.log" 2>&1; then
        cat "$OUT/obj/$ime.log"
        echo "GRESKA pri prevodjenju $ime.exe"
        exit 1
    fi
}

prevedi 1 v1_naivni
prevedi 2 v2_radix_cvorovi
prevedi 3 v3_radix_lanac
prevedi 4 v4_radix_csr
prevedi 5 v5_radix_lanac_pool
prevedi 6 v6_radix_lanac_opt

echo
echo "Gotovo. Programi su u $OUT/:"
ls "$OUT"/*.exe
