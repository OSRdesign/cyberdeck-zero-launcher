#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Host end-to-end tests of the P-a spike tools (Linux / WSL, no board, no evdev, no uinput needed):
#   * fbhandoff + fbhapp on a file-backed framebuffer, with a FIFO standing in for an input node (the app reads it,
#     a writer "types" Enter during the freeze): legacy SIGSTOP, drain through pidfd_getfd, the lifecycle contract,
#     a slow ack, a misbehaving app that draws while hidden, restore vs no restore, SIGTERM and SIGKILL safety,
#     the launcher refusal and --stop-launcher with a fake systemctl;
#   * mtrec --raw on a synthetic raw stream, its statistics against mta_analyse; mtrec --script over a FIFO.
#
#   bash tests/host_e2e.sh            (after: sh build.sh host)
set -u
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
B=$here/build/host
W=$(mktemp -d /tmp/mta-e2e.XXXXXX)
trap 'pkill -P $$ 2>/dev/null; [ -n "${KEEP:-}" ] && echo "kept $W" || rm -rf "$W"' EXIT
pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $*"; }
ko() { fail=$((fail + 1)); echo "FAIL $*"; }
check() { # name command...
    local name=$1
    shift
    if "$@"; then ok "$name"; else ko "$name"; fi
}
field() { # file key: value of key= in the last "rec=cycle" line
    grep 'rec=cycle' "$1" | tail -1 | tr ' ' '\n' | sed -n "s/^$2=//p"
}
num_ge() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 >= b + 0) }'; }
num_lt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 < b + 0) }'; }

for f in mtrec fbhandoff fbhapp; do
    [ -x "$B/$f" ] || { echo "missing $B/$f: run sh build.sh host" >&2; exit 2; }
done
FB=$W/fb.raw
GEOM=640x480x32

# writer: Enter (press, SYN, release, SYN) at each delay (seconds after start), monotonic timestamps
writer() { # fifo delays...
    python3 - "$@" << 'EOF'
import os, struct, sys, time
fifo, delays = sys.argv[1], [float(d) for d in sys.argv[2:]]
fd = os.open(fifo, os.O_WRONLY)
t0 = time.monotonic()
def ev(ty, code, val):
    t = time.monotonic()
    return struct.pack("<qqHHi", int(t), int((t % 1) * 1e6), ty, code, val)
for d in delays:
    time.sleep(max(0, t0 + d - time.monotonic()))
    os.write(fd, ev(1, 28, 1) + ev(0, 0, 0) + ev(1, 28, 0) + ev(0, 0, 0))
time.sleep(0.2)
os.close(fd)
EOF
}

# run_case NAME "fbhandoff options" "fbhapp options" writer-delays...
run_case() {
    local name=$1 hopts=$2 aopts=$3
    shift 3
    rm -f "$W/in.fifo"
    mkfifo "$W/in.fifo"
    # shellcheck disable=SC2086
    "$B/fbhandoff" --fb "$FB" --fb-geom $GEOM --after 1 --hold 2 --watch-ms 600 --end-after 0.3 \
        --input-match "$W/in.fifo" --out "$W/$name.txt" --app-log "$W/$name.app" $hopts -- \
        "$B/fbhapp" --fb "$FB" --fb-geom $GEOM --input "$W/in.fifo" --tag "$name" $aopts > "$W/$name.stdout" 2>&1 &
    local hp=$!
    if [ $# -gt 0 ]; then writer "$W/in.fifo" "$@" & fi
    wait $hp
    echo $? > "$W/$name.rc"
    wait
}

# ---------------------------------------------------------------- A: legacy app, nothing done about input
run_case legacy "--no-contract --input none" "" 1.8 2.2
check "legacy: frozen by SIGSTOP, no hide" test "$(field "$W/legacy.txt" hide)" = none -a "$(field "$W/legacy.txt" frozen)" = 1
check "legacy: input pending in the app's queue at thaw" test "$(field "$W/legacy.txt" app_pending)" = 1
check "legacy: the app acted on 2 stale Enters" test "$(grep -c 'kind=key code=28 .*stale=1 acted=1' "$W/legacy.app")" = 2
check "legacy: snapshot restored and verified" test "$(field "$W/legacy.txt" restore_verified)" = 1
check "legacy: no foreign write while frozen" test "$(field "$W/legacy.txt" foreign_writes)" = 0
check "legacy: app repainted after SIGCONT" num_ge "$(field "$W/legacy.txt" first_change_ms)" 0
check "legacy: app ended at the end" grep -q 'rec=end reason=cycles-done' "$W/legacy.txt"

# ---------------------------------------------------------------- B: legacy app, drained through pidfd_getfd
run_case drain "--no-contract --input drain" "" 1.8 2.2
check "drain: 8 events drained from the app's queue (2 Enters)" test "$(field "$W/drain.txt" drained_events)" = 8 -a "$(field "$W/drain.txt" drained_enters)" = 2
check "drain: the app saw no stale input" test "$(grep -c 'rec=input' "$W/drain.app")" = 0
check "drain: no pidfd error" test "$(field "$W/drain.txt" input_err)" = none

# ---------------------------------------------------------------- C: lifecycle contract, freeze after ack
run_case contract "--input none" "" 1.8
check "contract: hide acked in time" test "$(field "$W/contract.txt" hide)" = ack
check "contract: ack under 300 ms" num_lt "$(field "$W/contract.txt" ack_ms)" 300
check "contract: frozen after the ack" test "$(field "$W/contract.txt" frozen)" = 1
check "contract: show acked" num_ge "$(field "$W/contract.txt" show_ack_ms)" 0
check "contract: the app dropped the stale Enter itself on show" grep -q 'rec=show n=1 drained_events=[1-9]' "$W/contract.app"
check "contract: the app acted on nothing stale" test "$(grep -c 'stale=1 acted=1' "$W/contract.app")" = 0

# ---------------------------------------------------------------- D: slow ack -> timeout -> SIGSTOP
run_case slowack "--input none --ack-ms 200" "--ack-delay-ms 1000"
check "slow ack: hide timed out, group stopped" test "$(field "$W/slowack.txt" hide)" = timeout -a "$(field "$W/slowack.txt" frozen)" = 1

# ---------------------------------------------------------------- E: run in the background, misbehaving app
run_case drawhidden "--input none --background run" "--draw-hidden"
check "run+draw-hidden: not frozen" test "$(field "$W/drawhidden.txt" frozen)" = 0
check "run+draw-hidden: foreign writes seen on the card" num_ge "$(field "$W/drawhidden.txt" foreign_writes)" 1
run_case runhidden "--input none --background run" ""
check "run (well-behaved): no foreign writes" test "$(field "$W/runhidden.txt" foreign_writes)" = 0

# ---------------------------------------------------------------- F: restore matters for an app that does not repaint
run_case norestore "--no-contract --restore none" "--static"
check "no restore + static app: the card stays (residue > 90 %)" num_ge "$(field "$W/norestore.txt" card_residue_pct)" 90
run_case restore "--no-contract --restore snapshot" "--static"
check "restore + static app: no residue" test "$(field "$W/restore.txt" card_residue_pct)" = 0.00

# ---------------------------------------------------------------- G/H: signals during the hold
pixel_is_card() { # first bar of the card is (235,235,235)
    python3 -c "import sys; d=open('$FB','rb').read(); px=d[(10*640+20)*4:(10*640+20)*4+4]; sys.exit(0 if px[:3]==bytes([235,235,235]) else 1)"
}
pixel_not_card() { ! pixel_is_card; }
launcher_runs() { pgrep -x cyberdeckzero > /dev/null; }
for sig in TERM KILL; do
    rm -f "$W/in.fifo"
    mkfifo "$W/in.fifo"
    "$B/fbhandoff" --fb "$FB" --fb-geom $GEOM --after 0.5 --hold 30 --input-match "$W/in.fifo" \
        --out "$W/sig$sig.txt" --app-log "$W/sig$sig.app" --no-contract -- \
        "$B/fbhapp" --fb "$FB" --fb-geom $GEOM --input "$W/in.fifo" > /dev/null 2>&1 &
    hp=$!
    sleep 1.5
    check "SIG$sig: the card is up during the hold" pixel_is_card
    apppid=$(sed -n 's/.*rec=spawn pid=\([0-9]*\).*/\1/p' "$W/sig$sig.txt")
    kill -s "$sig" $hp
    wait $hp 2> /dev/null
    sleep 3.5
    check "SIG$sig: the panel is restored" pixel_not_card
    check "SIG$sig: the app group is gone" bash -c "! kill -0 -$apppid 2>/dev/null"
done
check "SIGTERM: restore on exit, clean end" grep -q 'rec=end reason=signal' "$W/sigTERM.txt"
check "SIGKILL: the guardian restored and ended the group" grep -q 'rec=guardian restored_fb=1 ended_group=1' "$W/sigKILL.txt"

# ---------------------------------------------------------------- I: the launcher owns the panel
mkdir -p "$W/fake"
cp "$(command -v sleep)" "$W/fake/cyberdeckzero"
"$W/fake/cyberdeckzero" 300 &
fakel=$!
sleep 0.3
"$B/fbhandoff" --fb "$FB" --fb-geom $GEOM --out "$W/refuse.txt" -- "$B/fbhapp" --fb "$FB" --fb-geom $GEOM \
    --input none > /dev/null 2>&1
check "refuses while a launcher runs (exit 3)" test $? = 3
cat > "$W/fake/systemctl" << EOF
#!/bin/sh
echo "\$*" >> "$W/systemctl.log"
case "\$2" in
    stop) pkill -x cyberdeckzero ;;
    start) "$W/fake/cyberdeckzero" 300 > /dev/null 2>&1 & ;;
esac
exit 0
EOF
chmod +x "$W/fake/systemctl"
MTA_SYSTEMCTL=$W/fake/systemctl "$B/fbhandoff" --fb "$FB" --fb-geom $GEOM --after 0.3 --hold 0.3 --watch-ms 100 \
    --end-after 0.1 --stop-launcher --out "$W/stop.txt" -- "$B/fbhapp" --fb "$FB" --fb-geom $GEOM --input none \
    > /dev/null 2>&1
check "--stop-launcher: stopped then started again" test "$(tr '\n' ';' < "$W/systemctl.log")" = "--user stop cyberdeckzero.service;--user start cyberdeckzero.service;"
check "--stop-launcher: the launcher runs again" launcher_runs
pkill -x cyberdeckzero
wait $fakel 2> /dev/null

# ---------------------------------------------------------------- J: mtrec on a raw stream vs the analyser
cd "$here" || exit 2
python3 mta_synth.py demo --board deck --csv "$W/truth.csv" --raw "$W/demo.bin"
"$B/mtrec" --raw --dev "$W/demo.bin" --abs 0,639,0,479,5 --board deck --swap 1 --panel-mm 56.6x42.5 \
    --out "$W/rec.csv" > "$W/mtrec.out" 2>&1
check "mtrec raw: exit 0" test $? = 0
python3 - "$W/rec.csv" "$W/truth.csv" "$W/mtrec.out" > "$W/cmp.out" 2>&1 << 'EOF'
import sys
sys.path.insert(0, ".")
import mta_analyse as A
rec, truth, out = A.load_recording(sys.argv[1]), A.load_recording(sys.argv[2]), open(sys.argv[3]).read()
r = A.analyse_recording(rec, sweep="none")["total"]
t = A.analyse_recording(truth, sweep="none")["total"]
line = [l for l in out.splitlines() if l.startswith("mtrec rec=total")][0]
c = dict(kv.split("=", 1) for kv in line.split()[1:])
bad = []
for k in ("max_fingers", "contacts", "short_lt30ms"):
    if not (int(c[k]) == r[k] == t[k]):
        bad.append((k, c[k], r[k], t[k]))
if abs(float(c["min_pair_mm"]) - r["min_pair_mm"]) > 0.05 or abs(r["min_pair_mm"] - t["min_pair_mm"]) > 0.05:
    bad.append(("min_pair_mm", c["min_pair_mm"], r["min_pair_mm"], t["min_pair_mm"]))
print("mismatch", bad) if bad else print("match", c["max_fingers"], c["contacts"], c["min_pair_mm"])
sys.exit(1 if bad else 0)
EOF
check "mtrec stats == analyser on its file == synthetic truth ($(cat "$W/cmp.out"))" test "$(head -c5 "$W/cmp.out")" = match

# ---------------------------------------------------------------- K: mtrec guided script over a FIFO
printf 'quiet|1.0|1|0|hands off\nhold|1.6|1|1.2|three fingers\n' > "$W/script.txt"
rm -f "$W/t.fifo"
mkfifo "$W/t.fifo"
"$B/mtrec" --raw --dev "$W/t.fifo" --abs 0,639,0,479,5 --board deck --script "$W/script.txt" --prep 0 --gap 0.2 \
    --out "$W/guided.csv" > "$W/guided.out" 2>&1 &
mp=$!
python3 - "$W/t.fifo" << 'EOF'
import os, sys, time
sys.path.insert(0, ".")
import mta_synth as S
s = S.Synth("deck")
s.hold(0, [(15, 20), (28, 18), (41, 20)], 700)  # starts ~1.4 s in: inside step "hold"
fd = os.open(sys.argv[1], os.O_WRONLY)
t0 = time.monotonic()
time.sleep(1.4)
base = int(time.monotonic() * 1e6)
os.write(fd, s.to_raw(base))
time.sleep(2.0)
os.close(fd)
EOF
wait $mp
check "guided: both steps marked" test "$(grep -c '^seg,' "$W/guided.csv")" = 4
python3 -c "
import sys; sys.path.insert(0, '.')
import mta_analyse as A
r = A.analyse_recording(A.load_recording('$W/guided.csv'), sweep='none')
sys.exit(0 if r['labels']['hold']['max_fingers_max'] == 3 and r['labels']['quiet']['max_fingers_max'] == 0 else 1)"
check "guided: the 3-finger hold falls in step 'hold', none in 'quiet'" test $? = 0
check "guided: mtrec printed the step feedback" grep -q 'seen: max 3 finger' "$W/guided.out"

echo
echo "host_e2e: $pass passed, $fail failed"
[ "$fail" = 0 ]
