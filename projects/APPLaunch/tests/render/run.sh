#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Render harness: build, render every scene at every size, compare with the goldens.
#
#   run.sh            build + render + compare with golden/ + header (exit 1 on any changed pixel)
#   run.sh build      build only (build/render-harness)
#   run.sh render [scene ...]   render scenes (default: all) into out/, contact sheets in out/sheets/
#   run.sh compare    compare out/ with golden/ (640x480 and 480x320), diff PNGs in out/diff/
#   run.sh header     page headers (scene command "header") against the home grid: status strip pixels, title origin
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
            # shots of a review-only scene (scene line "review") have no golden on purpose
            if [[ -f $out/$size/review.list ]] && grep -qxF "$(basename "$actual")" "$out/$size/review.list"; then
                continue
            fi
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

header() {
    # Every page header cut by a scene's "header NAME TITLE..." must show the home grid's status strip pixel for
    # pixel and its title at the same origin: out/<WxH>/header/<NAME>.png against home.png (render-harness compare),
    # <NAME>.txt ("x y text") against home.txt.
    local n=0 bad=0 dir size ref f name line ref_xy xy
    echo "== header: status strip and title origin against the home grid"
    for dir in "$out"/*/header; do
        [[ -d $dir ]] || continue
        size=$(basename "$(dirname "$dir")")
        ref="$dir/home"
        if [[ ! -f $ref.png || ! -f $ref.txt ]]; then
            echo "MISSING $size/header/home (the scene must take 'header home ZERO' first)"
            bad=$((bad + 1))
            continue
        fi
        ref_xy=$(cut -d' ' -f1,2 "$ref.txt")
        for f in "$dir"/*.png; do
            name=$(basename "$f" .png)
            [[ $name == home ]] && continue
            n=$((n + 1))
            if ! line=$("$bin" compare "$ref.png" "$f" --diff "$out/diff/header/$size/$name.png"); then
                echo "$size $name status strip: $line"
                bad=$((bad + 1))
            fi
            xy=$(cut -d' ' -f1,2 "$dir/$name.txt" 2>/dev/null || true)
            if [[ $xy != "$ref_xy" ]]; then
                echo "$size $name title origin ${xy:-?} ($(cut -d' ' -f3- "$dir/$name.txt" 2>/dev/null)), home $ref_xy"
                bad=$((bad + 1))
            fi
        done
    done
    if ((bad)); then
        echo "FAIL: $bad header check(s) of $n differ"
        return 1
    fi
    echo "PASS: $n headers match the home grid's strip and title origin"
}

golden() {
    local size f
    for size in $golden_sizes; do
        mkdir -p "$golden/$size"
        rm -f "$golden/$size"/*.png
        for f in "$out/$size"/*.png; do
            # review-only shots (scene line "review") never become goldens
            if [[ -f $out/$size/review.list ]] && grep -qxF "$(basename "$f")" "$out/$size/review.list"; then
                continue
            fi
            cp "$f" "$golden/$size/"
        done
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
    all) build; render; compare; header ;;
    build) build ;;
    render) build; render "$@" ;;
    compare) compare ;;
    header) header ;;
    golden) golden ;;
    validate) validate ;;
    twice) build; twice ;;
    -h|--help) sed -n '4,17p' "$0" ;;
    *) echo "unknown mode $mode (all|build|render|compare|header|golden|validate|twice)"; exit 2 ;;
esac
