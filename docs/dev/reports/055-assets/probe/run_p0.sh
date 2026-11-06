#!/bin/sh
# SPDX-License-Identifier: MIT
# P0 spike (report 055): run the probes on one host. Output: RESULT lines (and the host facts) on stdout.
#   sh run_p0.sh BIN_DIR TILE_DIR WORK_DIR section...
# sections: info verify decode decode_neon view read lru convert stub
# Everything runs under nice; RAM probes (lru, stub) run alone, one process at a time.
set -u
BIN=$1; T=$2; W=$3; shift 3
mkdir -p "$W"
N="nice -n 10"
P="$N $BIN/p0probe"
health() { echo "HEALTH $1 $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg) memavail_mb=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) throttled=$(vcgencmd get_throttled 2>/dev/null | cut -d= -f2)"; }
guard() {   # stop when the board runs short (the brief: free memory under ~60 MB or throttled non-zero)
    av=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)
    th=$(vcgencmd get_throttled 2>/dev/null | cut -d= -f2)
    if [ "$av" -lt 60 ] || { [ -n "$th" ] && [ "$th" != "0x0" ]; }; then echo "STOP guard memavail_mb=$av throttled=$th"; exit 3; fi
}
for s in "$@"; do
    guard
    health "before-$s"
    case $s in
    info)
        echo "INFO host=$(hostname) arch=$(uname -m) kernel=$(uname -r) cpus=$(nproc)"
        grep -m1 -E 'model name|Model' /proc/cpuinfo | sed 's/^/INFO /'
        grep -E 'Model|Hardware' /proc/cpuinfo | tail -1 | sed 's/^/INFO /'
        df -h "$W" | tail -1 | sed 's/^/INFO df /'
        findmnt -no SOURCE,FSTYPE -T "$W" | sed 's/^/INFO fs /'
        ;;
    verify)
        for f in png256 pal256 jpg256 png512 pal512 jpg512; do
            [ -f "$T/$f.mbtiles" ] && $P verify "$T/$f.pmtiles" "$T/$f.mbtiles" | sed "s/^RESULT verify/RESULT verify set=$f/"
        done
        ;;
    decode|decode_neon)
        PB=$BIN/p0probe; [ $s = decode_neon ] && PB=$BIN/p0probe_neon
        for f in png256 pal256 jpg256 png512 pal512 jpg512; do
            $N "$PB" decode "$T/$f.pmtiles" --lib stb --passes 3 | sed "s/^RESULT/RESULT $s/"
        done
        if [ $s = decode ]; then
            for f in png256 pal256 png512 pal512; do $P decode "$T/$f.pmtiles" --lib lodepng --passes 2; done
        fi
        ;;
    view)
        for c in 1 0; do
            for f in png256 pal256 jpg256; do
                $P view "$T/$f.pmtiles" --w 640 --h 480 --scale 1 --cold $c
                $P view "$T/$f.pmtiles" --w 640 --h 480 --scale 2 --cold $c
                $P view "$T/$f.pmtiles" --w 480 --h 320 --scale 1.5 --cold $c
                $P view "$T/$f.pmtiles" --w 320 --h 480 --scale 1.5 --cold $c
            done
            for f in png512 pal512 jpg512; do
                $P view "$T/$f.pmtiles" --w 640 --h 480 --scale 1 --cold $c
                $P view "$T/$f.pmtiles" --w 480 --h 320 --scale 1 --cold $c
            done
        done
        $P view "$T/pal256.pmtiles" --w 480 --h 320 --scale 1.5 --cold 1 --mag bil
        $P view "$T/pal256.pmtiles" --w 640 --h 480 --scale 2 --cold 1 --workers 1
        $P view "$T/pal256.pmtiles" --w 640 --h 480 --scale 2 --cold 1 --workers 4
        ;;
    mag)
        $P decode "$T/pal256.pmtiles" --passes 1 | grep -E 'RESULT (decode|magnify)'
        ;;
    read)
        for f in pal256 png256; do
            for m in pread mmap; do $P read --method $m "$T/$f.pmtiles"; done
            for m in sqlite sqlmmap; do $P read --method $m "$T/$f.mbtiles"; done
        done
        ;;
    lru)
        L="$T/pal256.pmtiles"
        for sl in 64 128; do for b in 2 3 4; do $P lru "$L" --slots $sl --bpp $b --scale 1; done; done
        for sl in 12 16 32; do $P lru "$L" --slots $sl --bpp 2 --scale 2; done
        for sl in 16 32; do $P lru "$T/pal512.pmtiles" --slots $sl --bpp 2 --scale 1; done
        ;;
    convert)
        sync
        $P convert "$T/pal256" "$W/conv_pal256.mbtiles" --batch 256
        rm -f "$W"/conv_*.mbtiles*
        $P convert "$T/pal256" "$W/conv_pal256.mbtiles" --batch 1 --max 300
        rm -f "$W"/conv_*.mbtiles*
        $P convert "$T/pal256" "$W/conv_pal256.mbtiles" --batch 256 --max 300
        rm -f "$W"/conv_*.mbtiles*
        if [ -d "$T/png256" ]; then
            $P convert "$T/png256" "$W/conv_png256.mbtiles" --batch 256
            rm -f "$W"/conv_*.mbtiles*
        fi
        ;;
    stub)
        for k in empty static static_trim dlopen; do
            for mb in 4 8; do
                [ $k = empty ] && [ $mb = 8 ] && continue
                $N "$BIN/stub_$k" "$T/pal256.mbtiles" $mb | sed "s/^RESULT stubmem/RESULT stubmem kind=$k cache_mb=$mb/"
            done
        done
        ;;
    esac
    health "after-$s"
done
