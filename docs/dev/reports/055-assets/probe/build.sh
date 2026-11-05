#!/bin/sh
# SPDX-License-Identifier: MIT
# P0 spike (report 055): build the throwaway probes in WSL, native x86_64 and arm64 (cross), in fresh build dirs on the
# WSL file system (never /mnt/c, never ~/cdk-build), under the build box lock, -j3.
#   wsl -e sh /mnt/c/CLAUDE/zero7/maps-p0-work/probe/build.sh
set -eu
SRC_WIN=/mnt/c/CLAUDE/zero7/maps-p0-work
ROOT=$HOME/maps-p0
mkdir -p "$ROOT"
rm -rf "$ROOT/src"
mkdir -p "$ROOT/src"
cp -r "$SRC_WIN/probe" "$ROOT/src/probe"
mkdir -p "$ROOT/src/third_party/sqlite" "$ROOT/src/third_party/stb" "$ROOT/src/third_party/lodepng"
cp -r "$SRC_WIN/third_party/sqlite/sqlite-amalgamation-3500400" "$ROOT/src/third_party/sqlite/"
cp "$SRC_WIN/third_party/stb/stb_image.h" "$ROOT/src/third_party/stb/"
cp "$SRC_WIN/third_party/lodepng/lodepng.cpp" "$SRC_WIN/third_party/lodepng/lodepng.h" "$ROOT/src/third_party/lodepng/"
find "$ROOT/src" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.sh' \) -exec sed -i 's/\r$//' {} +

SQLITE="$ROOT/src/third_party/sqlite/sqlite-amalgamation-3500400/sqlite3.c"
TRIM="-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_OMIT_PROGRESS_CALLBACK \
 -DSQLITE_OMIT_JSON -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 -DSQLITE_OMIT_UTF16 -DSQLITE_OMIT_COMPLETE \
 -DSQLITE_OMIT_GET_TABLE -DSQLITE_OMIT_TRACE -DSQLITE_OMIT_AUTHORIZATION -DSQLITE_OMIT_DECLTYPE -DSQLITE_MAX_EXPR_DEPTH=0"

build_arch() {
    arch=$1; CC=$2; CXX=$3; STRIP=$4; TP=$5
    B="$ROOT/build-$arch"
    rm -rf "$B"; mkdir -p "$B"; cd "$B"
    P="$ROOT/src/probe"
    # objects in parallel (3 jobs)
    $CC -O2 -c "$SQLITE" -o sqlite3_probe.o -DSQLITE_OMIT_LOAD_EXTENSION &
    $CC -O2 -c "$SQLITE" -o sqlite3_def.o &
    $CC -Os -c "$SQLITE" -o sqlite3_trim.o $TRIM &
    wait
    $CC -O2 -Wall -c "$P/stbimpl.c" -o stb.o &
    $CC -O2 -Wall -DP0_STB_STATIC -c "$P/stbimpl.c" -o stb_static.o &
    $CC -O2 -x c -c "$ROOT/src/third_party/lodepng/lodepng.cpp" -o lodepng.o &
    wait
    if [ "$arch" = arm64 ]; then $CC -O2 -Wall -DP0_STB_NEON -c "$P/stbimpl.c" -o stb_neon.o; fi
    $CC -O2 -Wall -std=gnu11 -c "$P/p0probe.c" -o p0probe.o
    $CC -O2 -Wall -std=gnu11 -c "$P/pmt.c" -o pmt.o
    $CC -static -o p0probe p0probe.o pmt.o stb.o lodepng.o sqlite3_probe.o -lpthread -lm
    if [ "$arch" = arm64 ]; then
        $CC -static -o p0probe_neon p0probe.o pmt.o stb_neon.o lodepng.o sqlite3_probe.o -lpthread -lm
    fi
    LDF="-Wl,--as-needed"
    $CXX -O2 -DSTUB_EMPTY "$P/stub.cpp" -o stub_empty $LDF
    $CXX -O2 -DSTUB_STATIC "$P/stub.cpp" stb.o sqlite3_def.o -o stub_static $LDF -lpthread -ldl -lm
    $CXX -O2 -DSTUB_STATIC "$P/stub.cpp" stb.o sqlite3_trim.o -o stub_static_trim $LDF -lpthread -ldl -lm
    $CXX -O2 -DSTUB_DLOPEN "$P/stub.cpp" stb.o -o stub_dlopen $LDF -ldl
    for f in stub_empty stub_static stub_static_trim stub_dlopen; do $STRIP --strip-all -o "$f.stripped" "$f"; done
    echo "== $arch sizes (stripped bytes)"
    for f in stub_empty stub_static stub_static_trim stub_dlopen; do
        printf '%s %s\n' "$f" "$(stat -c %s "$f.stripped")"
    done
    for o in stb.o stb_static.o lodepng.o sqlite3_def.o sqlite3_trim.o; do
        printf 'obj %s text+data=%s globals_stbi=%s\n' "$o" \
            "$(${TP}size "$o" 2>/dev/null | awk 'NR==2{print $1+$2}' || echo ?)" \
            "$(${TP}nm "$o" 2>/dev/null | grep -c ' T stbi_' || true)"
    done
    mkdir -p bin
    cp p0probe bin/; [ -f p0probe_neon ] && cp p0probe_neon bin/
    for f in stub_empty stub_static stub_static_trim stub_dlopen; do cp "$f.stripped" "bin/$f"; done
    cp "$ROOT/src/probe/run_p0.sh" bin/
    ls -la bin
}

pgrep -a 'cc1|scons|ninja|make' && echo "note: other compilers are running" || true
(
    flock 9
    build_arch amd64 gcc g++ strip ""
    build_arch arm64 aarch64-linux-gnu-gcc aarch64-linux-gnu-g++ aarch64-linux-gnu-strip aarch64-linux-gnu-
) 9>/tmp/wsl-build.lock
