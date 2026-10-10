#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Render harness: build, render every scene at every size, compare with the goldens.
#
#   run.sh            build + render + compare with golden/ (exit 1 on any changed pixel)
#   run.sh build      build only (build/render-harness)
#   run.sh render [scene ...]   render scenes (default: all) into out/, contact sheets in out/sheets/
#   run.sh compare    compare out/ with golden/ (640x480 and 480x320), diff PNGs in out/diff/
#   run.sh golden     copy out/640x480 and out/480x320 into golden/ (a deliberate baseline change)
#   run.sh validate   compare the harness with real device captures (reference/, docs/screenshots/)
#   run.sh twice      render everything twice and check the PNGs are byte-identical
#
# Environment: JOBS (make -j, default nproc), OUT (default out/). Runs in WSL/Linux with gcc, g++,
# pkg-config, FreeType and libpng headers; no SDL. See README.md.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
bin="$here/build/render-harness"
out=${OUT:-$here/out}
golden="$here/golden"
jobs=${JOBS:-$(nproc)}
golden_sizes="640x480 480x320"

build() {
    mkdir -p "$here/build"
    echo "== build ($jobs jobs)"
    if ! make -C "$here" -j"$jobs" >"$here/build/make.log" 2>&1; then
        grep -E "error|Error" "$here/build/make.log" | head -30
        echo "build failed: $here/build/make.log"
        exit 2
    fi
}

render() { # [scene names or paths]
    local scenes=() s rc=0
    if (($#)); then
        for s in "$@"; do
            [[ $s == *.scene ]] || s="$here/scenes/$s.scene"
            scenes+=("$s")
        done
    else
        scenes=("$here"/scenes/*.scene)
        rm -rf "$out"
    fi
    echo "== render ${#scenes[@]} scene(s) into $out"
    for s in "${scenes[@]}"; do
        if ! "$bin" render "$s" --out "$out" >/dev/null; then
            echo "scene failed: $s"
            rc=1
        fi
    done
    echo "   $(find "$out" -path "$out/sheets" -prune -o -path "$out/diff" -prune -o -name '*.png' -print | wc -l) screenshots, sheets in $out/sheets/"
    return $rc
}

compare() {
    local n=0 bad=0 g size name actual line
    echo "== compare with $golden"
    rm -rf "$out/diff"
    for g in "$golden"/*/*.png; do
        [[ -f $g ]] || continue
        size=$(basename "$(dirname "$g")")
        name=$(basename "$g")
        actual="$out/$size/$name"
        n=$((n + 1))
        if [[ ! -f $actual ]]; then
            echo "MISSING $size/$name (no longer rendered)"
            bad=$((bad + 1))
            continue
        fi
        if ! line=$("$bin" compare "$g" "$actual" --diff "$out/diff/$size/$name"); then
            echo "$line"
            bad=$((bad + 1))
        fi
    done
    for size in $golden_sizes; do
        for actual in "$out/$size"/*.png; do
            [[ -f $actual ]] || continue
            if [[ ! -f $golden/$size/$(basename "$actual") ]]; then
                echo "NEW $size/$(basename "$actual") has no golden (run.sh golden to accept it)"
                bad=$((bad + 1))
            fi
        done
    done
    if ((bad)); then
        echo "FAIL: $bad of $n goldens differ or are missing (diff images in $out/diff/)"
        return 1
    fi
    echo "PASS: $n goldens identical"
}

golden() {
    local size
    for size in $golden_sizes; do
        mkdir -p "$golden/$size"
        rm -f "$golden/$size"/*.png
        cp "$out/$size"/*.png "$golden/$size/"
    done
    echo "golden/ updated from $out ($(ls "$golden"/*/*.png | wc -l) images): review and commit deliberately"
}

validate() {
    # Harness against real device captures (v0.5.0). Expected results are in README.md.
    local ref="$here/reference" rc=0
    echo "== validate against device captures"
    echo "-- deck home 640x480 (deck /dev/fb0 32 bpp): expect identical"
    "$bin" compare "$ref/deck-home-640x480.png" "$out/640x480/home.png" \
        --diff "$out/diff/validate-deck-home.png" || rc=1
    echo "-- Pi 3A+ home 480x320 (/dev/fb1 RGB565, de-rotated): fbcon cursor at the top left ignored;"
    echo "   expect at most 1 LSB of RGB565 (channel delta <= 9) in the clock text"
    "$bin" compare "$ref/pi3a-home-480x320.png" "$out/480x320/home.png" --ignore 0,0,14,30 \
        --diff "$out/diff/validate-pi3a-home.png" || true
    "$bin" compare "$ref/pi3a-home-480x320.png" "$out/480x320/home.png" --ignore 0,0,14,30 --tolerance 9 || rc=1
    echo "-- Settings root 640x480 (docs/screenshots/settings.png, deck 2026-10-03): clock and Wi-Fi"
    echo "   strip ignored (other time and signal); expect identical elsewhere"
    "$bin" compare "$root/docs/screenshots/settings.png" "$out/640x480/settings_root.png" --ignore 440,0,200,40 \
        --diff "$out/diff/validate-settings-root.png" || rc=1
    return $rc
}

twice() {
    local first="$out" second="$out-again" n=0 bad=0 f
    render
    out="$second"
    render
    out="$first"
    while IFS= read -r f; do
        n=$((n + 1))
        cmp -s "$first/$f" "$second/$f" || { echo "NOT IDENTICAL: $f"; bad=$((bad + 1)); }
    done < <(cd "$first" && find . -name '*.png' | sort)
    rm -rf "$second"
    if ((bad)); then
        echo "FAIL: $bad of $n PNGs differ between two runs"
        return 1
    fi
    echo "PASS: $n PNGs byte-identical over two runs"
}

mode=${1:-all}
(($#)) && shift
case $mode in
    all) build; render; compare ;;
    build) build ;;
    render) build; render "$@" ;;
    compare) compare ;;
    golden) golden ;;
    validate) validate ;;
    twice) build; twice ;;
    -h|--help) sed -n '4,16p' "$0" ;;
    *) echo "unknown mode $mode (all|build|render|compare|golden|validate|twice)"; exit 2 ;;
esac
