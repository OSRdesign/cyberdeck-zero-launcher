#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Render harness: build, render every scene at every size, compare with the goldens.
#
#   run.sh            build + render + compare with golden/ + header (exit 1 on any changed pixel)
#   run.sh build      build only (build/render-harness)
#   run.sh render [scene ...]   render scenes (default: all) into out/, contact sheets in out/sheets/
#   run.sh compare    compare out/ with golden/ (640x480, 480x320, the PDA's 480x640 and 480x800), diff PNGs in out/diff/
#   run.sh header     page headers (scene command "header") against the home grid: status strip pixels, title origin
#   run.sh golden     copy out/640x480, out/480x320, out/480x640 and out/480x800 into golden/ (a deliberate change)
#   run.sh validate   compare the harness with real device captures (reference/, docs/screenshots/)
#   run.sh twice      render everything twice and check the PNGs are byte-identical
#   run.sh contract   app contract: the host app-contract-probe --report against the metrics scene (docs/APP-CONTRACT.md)
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
golden_sizes="640x480 480x320 480x640 480x800" # 480x640, 480x800: the PDA scenes (report 038 S2, profile pda)

build() {
    mkdir -p "$here/build"
    echo "== build ($jobs jobs)"
    # the SDK's cJSON (Settings > Updates, report 047): the scons build downloads it, a seeded build tree may lack it
    sh "$root/projects/APPLaunch/tests/ensure_cjson.sh" "$root/SDK" >/dev/null
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
            echo "MISSING $size/header/home (the scene must take 'header home' first)"
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
    return $rc
}

contract() {
    # App contract 1 (docs/APP-CONTRACT.md, task 014 P5-3): the host build of tools/dev/app-contract-probe reads, in
    # --report mode, what an app started by this launcher would see: the per-launch environment the metrics scene
    # wrote with the launcher's own builder (out/<WxH>/metrics.{compat,fullscreen}.env = the APPLAUNCH_SCREEN_* panel
    # variables + app_launch_env::export_for_current), screen.state from the harness's runtime directory
    # (out/<WxH>/runtime/applaunch) and an input.state fixture written there (vkb=force, hand=1). Its facts must equal the
    # layout service's numbers (out/<WxH>/metrics.facts): size, class, density, rotation, canvas, window, scale. Then the
    # degrade rules: no environment (screen.state only) and nothing at all.
    local probe_dir="$root/tools/dev/app-contract-probe" probe size dir facts mode rep n=0 bad=0 sizes_done=0
    local want_size want_class want_ppmm want_rot want_compat want_scale w h canvas window scale cppmm
    probe="$probe_dir/build/host/app-contract-probe"
    echo "== contract: app-contract-probe --report against the harness's runtime directory"
    if ! sh "$probe_dir/build.sh" host >"$here/build/probe.log" 2>&1; then
        cat "$here/build/probe.log"
        echo "FAIL: the probe does not build"
        return 1
    fi
    render metrics >/dev/null || { echo "FAIL: the metrics scene did not render"; return 1; }

    fact() { sed -n "s/^$1=//p" "$facts" | head -1; }
    expect() { # file key value
        local got
        got=$(sed -n "s/^$2=//p" "$1" | head -1)
        n=$((n + 1))
        if [[ $got != "$3" ]]; then
            echo "$size $(basename "$1"): $2=$got, expected $3"
            bad=$((bad + 1))
        fi
    }

    for size in 480x320 640x480 800x480 720x720 1280x720 1024x600 480x640 480x800; do
        dir="$out/$size"
        facts="$dir/metrics.facts"
        if [[ ! -f $facts ]]; then
            [[ $size == 1024x600 ]] && continue
            echo "MISSING $size/metrics.facts (the metrics scene must render at $size)"
            bad=$((bad + 1))
            continue
        fi
        sizes_done=$((sizes_done + 1))
        want_size=$(fact size) want_class=$(fact class) want_ppmm=$(fact ppmm) want_rot=$(fact rot)
        want_compat=$(fact compat) want_scale=$(fact scale)
        w=${want_size%x*} h=${want_size#*x}
        [[ $size == "$want_size" ]] || { echo "$size: the layout service says $want_size"; bad=$((bad + 1)); }
        printf 'v=1\nkeyboards=0\nbt_keyboards=0\nlast_seen=0\nkb_gone=0\nbt_gone=0\nvkb=force\nhand=1\ngen=7\n' \
            >"$dir/runtime/applaunch/input.state"

        for mode in compat fullscreen; do
            rep="$dir/contract.$mode.txt"
            env -i bash -c ". '$dir/metrics.$mode.env' && exec '$probe' --report --no-fb --tag $mode" >"$rep"
            if [[ $mode == compat ]]; then
                canvas=320x170 window=$want_compat scale=$want_scale
            else
                canvas=$want_size window=0,0,$w,$h scale=1
            fi
            cppmm=$(((want_ppmm + scale / 2) / scale))
            expect "$rep" env.ok 1
            expect "$rep" env.contract 1
            expect "$rep" env.state_dir "$dir/runtime/applaunch"
            expect "$rep" env.screen_class "$want_class"
            expect "$rep" screen.ok 1
            expect "$rep" screen.size "$want_size"
            expect "$rep" screen.class "$want_class"
            expect "$rep" screen.ppmm "$want_ppmm"
            expect "$rep" screen.rot "$want_rot"
            expect "$rep" screen.compat "$want_compat"
            expect "$rep" screen.scale "$want_scale"
            expect "$rep" screen.kb_inset 0
            expect "$rep" input.ok 1
            expect "$rep" input.vkb force
            expect "$rep" input.hand 1
            expect "$rep" env.pda 0
            expect "$rep" derived.source contract
            expect "$rep" derived.mode "$mode"
            expect "$rep" derived.screen "$want_size"
            expect "$rep" derived.rot "$want_rot"
            expect "$rep" derived.class "$want_class"
            expect "$rep" derived.ppmm "$want_ppmm"
            expect "$rep" derived.canvas "$canvas"
            expect "$rep" derived.window "$window"
            expect "$rep" derived.canvas_scale "$scale"
            expect "$rep" derived.canvas_ppmm "$cppmm"
            expect "$rep" derived.tokens_from file
            expect "$rep" derived.osk_allowed 1
            expect "$rep" summary "$mode $canvas $window scale $scale $want_class $want_ppmm"
        done

        # report 038 S2 (the PDA profile): a landscape fullscreen app on a portrait launcher runs turned; through the
        # environment it sees the panel's landscape canvas, a quarter turn back (screen.state stays the launcher's panel)
        if [[ -f $dir/metrics.turned.env ]]; then
            local turned_size=${h}x${w} turned_class
            case $turned_size in 800x480) turned_class=wide ;; 640x480) turned_class=standard ;; *) turned_class=? ;; esac
            rep="$dir/contract.turned.txt"
            env -i bash -c ". '$dir/metrics.turned.env' && exec '$probe' --report --no-fb --tag turned" >"$rep"
            expect "$rep" env.ok 1
            expect "$rep" env.pda 0
            expect "$rep" env.screen_class "$turned_class"
            expect "$rep" screen.size "$want_size"
            expect "$rep" screen.rot "$want_rot"
            expect "$rep" derived.source contract
            expect "$rep" derived.mode fullscreen
            expect "$rep" derived.screen "$turned_size"
            expect "$rep" derived.rot "$(((want_rot + 270) % 360))"
            expect "$rep" derived.class "$turned_class"
            expect "$rep" derived.ppmm "$want_ppmm"
            expect "$rep" derived.canvas "$turned_size"
            expect "$rep" derived.window "0,0,$h,$w"
            expect "$rep" derived.canvas_scale 1
        fi

        # degrade: started without the contract (ssh, an older launcher) but screen.state is there
        rep="$dir/contract.nocontract.txt"
        env -i XDG_RUNTIME_DIR="$dir/runtime" "$probe" --report --no-fb --tag nocontract >"$rep"
        expect "$rep" env.ok 0
        expect "$rep" derived.source state
        expect "$rep" derived.mode none
        expect "$rep" derived.canvas "$want_size"
        expect "$rep" derived.class "$want_class"
        expect "$rep" derived.ppmm "$want_ppmm"
    done

    # degrade: nothing at all (no environment, no file): still exit 0, defaults
    rep="$out/contract.nothing.txt"
    if ! env -i APPLAUNCH_STATE_DIR=/nonexistent "$probe" --report --no-fb --tag nothing >"$rep"; then
        echo "nothing: the probe did not exit 0"
        bad=$((bad + 1))
    fi
    size=none
    expect "$rep" derived.source none
    expect "$rep" derived.ppmm 1130
    expect "$rep" derived.tokens_from none

    if ((bad)); then
        echo "FAIL: $bad of $n contract checks differ ($sizes_done sizes; reports in $out/<WxH>/contract.*.txt)"
        return 1
    fi
    echo "PASS: $n contract checks at $sizes_done sizes (reports in $out/<WxH>/contract.*.txt)"
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
    contract) build; contract ;;
    -h|--help) sed -n '4,18p' "$0" ;;
    *) echo "unknown mode $mode (all|build|render|compare|header|golden|validate|twice|contract)"; exit 2 ;;
esac
