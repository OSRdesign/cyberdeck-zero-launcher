#!/bin/sh
# PC test of install.sh's board detection and profile handling, and of uninstall.sh's plan, against the fake
# /proc and /sys trees in fixtures/ (SYSROOT and UNINSTALL_ROOT test hooks), and of default-sources.sh (the default
# app source) against a temp state folder and a fake Store backend. Changes nothing outside a temp dir,
# needs no root and no Pi. Run:   sh projects/APPLaunch/pizero2w/tests/test-install-board.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
INSTALL="$HERE/../install.sh"
UNINSTALL="$HERE/../uninstall.sh"
FIX="$HERE/fixtures"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export INSTALL_GLIBC_VERSION=2.41
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); printf 'ok   %s\n' "$1"; }
ko() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$1"; [ -z "${2:-}" ] || printf '%s\n' "$2" | sed 's/^/     | /'; }
# fresh copy of a fixture
root() { rm -rf "$TMP/$1"; cp -R "$FIX/$1" "$TMP/$1"; echo "$TMP/$1"; }
# run install.sh; output in $OUT, exit code in $RC
run() { OUT=$(SYSROOT="$R" sh "$INSTALL" "$@" 2>&1); RC=$?; }
has() { case "$OUT" in *"$1"*) return 0 ;; esac; return 1; }
check() { if eval "$2"; then ok "$1"; else ko "$1" "$OUT"; fi; }

# --- deck: detected, no profile, no gpio rule, PWM boot config as before
R=$(root zero2w)
run --dry-run
check "zero2w dry-run: exit 0" '[ $RC = 0 ]'
check "zero2w dry-run: board zero2w" 'has "board: zero2w"'
check "zero2w dry-run: no profile file" 'has "zero2w: no profile file"'
check "zero2w dry-run: no gpio backlight rule" '! has "92-applaunch-backlight-gpio"'
check "zero2w dry-run: PWM boot config as today" 'has "boot config: PWM backlight overlay"'
check "zero2w dry-run: nothing written" '[ ! -e "$R/etc" ]'
run --board-only
check "zero2w board-only: no profile written" '[ $RC = 0 ] && [ ! -e "$R/etc/applaunch/board.conf" ] && [ ! -e "$R/etc/udev" ]'

# --- Pi 3A+ with the Luckfox 3.5" panel
R=$(root pi3a-luckfox35)
run --dry-run
check "pi3a dry-run: exit 0" '[ $RC = 0 ]'
check "pi3a dry-run: board pi3a-luckfox35" 'has "board: pi3a-luckfox35"'
check "pi3a dry-run: fb1 picked" 'has "APPLAUNCH_FB=/dev/fb1"'
check "pi3a dry-run: panel size for the layout service" 'has "APPLAUNCH_PANEL_MM=79x49"'
check "pi3a dry-run: gpio backlight rule planned" 'has "92-applaunch-backlight-gpio.rules"'
check "pi3a dry-run: no PWM overlay" 'has "cmdline cursor/consoleblank only"'
check "pi3a dry-run: nothing written" '[ ! -e "$R/etc" ]'
run --board-only
P="$R/etc/applaunch/board.conf"
check "pi3a board-only: profile written" '[ $RC = 0 ] && [ -f "$P" ]'
check "pi3a board-only: gpio rule written" '[ -f "$R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules" ]'
EXPECT='APPLAUNCH_BOARD=pi3a-luckfox35
APPLAUNCH_FB=/dev/fb1
APPLAUNCH_LOGICAL=480x320
APPLAUNCH_ROTATE=90
APPLAUNCH_COMPAT_SCALE=1
APPLAUNCH_PANEL_MM=79x49
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio
LV_LINUX_FBDEV_DEVICE=/dev/fb1
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0'
GOT=$(grep -v '^#' "$P")
if [ "$GOT" = "$EXPECT" ]; then ok "pi3a profile: exact keys and values"; else ko "pi3a profile: exact keys and values" "$GOT"; fi
# shellcheck disable=SC1090
GOT=$( ( set -a; . "$P"; echo "$APPLAUNCH_FB $APPLAUNCH_ROTATE $APPLAUNCH_TOUCH_ORIENT $APPLAUNCH_PANEL_MM" ) )
check "pi3a profile: valid sh syntax" '[ "$GOT" = "/dev/fb1 90 buffer 79x49" ]'
check "pi3a profile: only KEY=VALUE or comment lines" '! grep -v -E "^(#.*|[A-Z_][A-Z0-9_]*=[^ ]*)$" "$P" >/dev/null'
SUM1=$(cksum < "$P")
run --board-only
check "pi3a second run: idempotent" '[ $RC = 0 ] && has "already up to date" && [ "$(cksum < "$P")" = "$SUM1" ] && [ ! -e "$P.bak" ]'
sed -i 's/^APPLAUNCH_ROTATE=90/APPLAUNCH_ROTATE=270/' "$P"
EDITED=$(cksum < "$P")
run --board-only
check "pi3a hand-edited: kept without --force" 'has "edited by hand: kept" && [ "$(cksum < "$P")" = "$EDITED" ] && [ ! -e "$P.bak" ]'
run --board-only --force
check "pi3a hand-edited + --force: replaced, .bak kept" '[ "$(cksum < "$P")" = "$SUM1" ] && grep -q "^APPLAUNCH_ROTATE=270" "$P.bak"'

# --- the panel's framebuffer is found by name, not assumed to be fb1
R=$(root pi3a-luckfox35)
mv "$R/sys/class/graphics/fb1" "$R/sys/class/graphics/fb0"
run --dry-run
check "pi3a panel on fb0: APPLAUNCH_FB=/dev/fb0" 'has "APPLAUNCH_FB=/dev/fb0" && has "LV_LINUX_FBDEV_DEVICE=/dev/fb0"'

# --- 3A+ whose SPI connector is not connected: unknown
R=$(root pi3a-luckfox35)
echo disconnected > "$R/sys/class/drm/card0-SPI-1/status"
run --dry-run
check "pi3a without connected panel: unknown" 'has "board: unknown" && has "probe-board.sh"'

# --- unknown board: facts printed, nothing written, install stops
R=$(root unknown-pi5-hyperpixel)
run --dry-run
check "unknown dry-run: facts and probe advice" '[ $RC = 0 ] && has "board: unknown" && has "Raspberry Pi 5" && has "drm card2-DPI-1: connected" && has "drm-rp1-dpidrmf" && has "probe-board.sh"'
check "unknown dry-run: says install would stop" 'has "would stop here"'
run --board-only
check "unknown board-only: nothing written" '[ $RC = 0 ] && [ ! -e "$R/etc" ]'

# --- forced profiles
R=$(root zero2w)
run --dry-run --board pi3a-luckfox35
check "--board pi3a on a deck (no panel fb): refused, exit 1" '[ $RC = 1 ] && has "no 320x480"'
run --dry-run --board nosuch
check "--board unknown name: exit 2" '[ $RC = 2 ]'
R=$(root pi3a-luckfox35)
run --dry-run --board zero2w
check "--board zero2w on a 3A+: no profile" 'has "board: zero2w (forced" && has "zero2w: no profile file"'
R=$(root unknown-pi5-hyperpixel)
mkdir -p "$R/sys/class/graphics/fb1" && cp "$FIX/pi3a-luckfox35/sys/class/graphics/fb1/"* "$R/sys/class/graphics/fb1/"
run --dry-run --board pi3a-luckfox35
check "--board pi3a with a matching fb: profile planned" '[ $RC = 0 ] && has "APPLAUNCH_FB=/dev/fb1"'

# --- deck with a stale profile: generated one moved away, hand-edited one kept
R=$(root pi3a-luckfox35); run --board-only; STALE="$R/etc/applaunch/board.conf"
D=$(root zero2w); mkdir -p "$D/etc/applaunch"; cp "$STALE" "$D/etc/applaunch/board.conf"
R=$D; run --board-only
check "zero2w stale generated profile: moved to .bak" '[ ! -e "$R/etc/applaunch/board.conf" ] && [ -f "$R/etc/applaunch/board.conf.bak" ]'
D=$(root zero2w); mkdir -p "$D/etc/applaunch"; printf 'APPLAUNCH_BOARD=mine\n' > "$D/etc/applaunch/board.conf"
R=$D; run --board-only
check "zero2w hand-written profile: kept" '[ -f "$R/etc/applaunch/board.conf" ] && has "edited by hand: kept"'

# --- glibc floor
R=$(root pi3a-luckfox35)
OUT=$(INSTALL_GLIBC_VERSION=2.36 SYSROOT="$R" sh "$INSTALL" --board-only 2>&1); RC=$?
check "glibc 2.36: refused, exit 1, nothing written" '[ $RC = 1 ] && has "glibc 2.36" && has "2.38" && [ ! -e "$R/etc" ]'
OUT=$(INSTALL_GLIBC_VERSION=2.38 SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "glibc 2.38: accepted" '[ $RC = 0 ]'
OUT=$(INSTALL_GLIBC_VERSION=3.0 SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "glibc 3.0: accepted" '[ $RC = 0 ]'
OUT=$(INSTALL_GLIBC_VERSION= SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "glibc of this host read without the hook: exit 0 (host glibc >= 2.38) or 1 with the message" '[ $RC = 0 ] || has "need glibc 2.38"'

# --- the SYSROOT hook never runs a real install
OUT=$(SYSROOT="$TMP/none" sh "$INSTALL" 2>&1); RC=$?
check "SYSROOT without --dry-run/--board-only: refused" '[ $RC = 2 ]'

# --- uninstall.sh plan (dry run only) removes the profile and the gpio rule
R=$(root pi3a-luckfox35); run --board-only
mkdir -p "$TMP/home"
OUT=$(HOME="$TMP/home" UNINSTALL_ROOT="$R" sh "$UNINSTALL" --dry-run --no-boot-config 2>&1); RC=$?
check "uninstall dry-run: exit 0" '[ $RC = 0 ]'
check "uninstall dry-run: removes board.conf" 'has "rm -f $R/etc/applaunch/board.conf"'
check "uninstall dry-run: removes the gpio rule" 'has "rm -f $R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules"'
check "uninstall dry-run: changed nothing" '[ -f "$R/etc/applaunch/board.conf" ]'

# --- default app source: install.sh's plan and option
R=$(root zero2w)
run --dry-run
check "install dry-run: default app source planned" 'has "default app source: OSRdesign/cyberdeck-zero-apps added for the user once"'
run --dry-run --no-default-source
check "install dry-run --no-default-source: skipped" '[ $RC = 0 ] && has "default app source: skipped (--no-default-source)"'
check "install.sh runs default-sources.sh as the user" 'grep -q "runuser -u \"\$TARGET_USER\" -- env HOME=\"\$TARGET_HOME\" sh \"\$HERE/default-sources.sh\"" "$INSTALL"'
check "build.sh puts default-sources.sh in the bundle" 'grep -q "\$HERE/default-sources.sh" "$HERE/../build.sh"'

# --- default-sources.sh against a temp state folder (M5APPSTORE_STATE_DIR) and a fake Store backend
SEED="$HERE/../default-sources.sh"
SRC_URL=https://raw.githubusercontent.com/OSRdesign/cyberdeck-zero-apps/main/registry.json
FAKE="$TMP/fake-appstore"
LOG="$TMP/fake-appstore.log"
cat > "$FAKE" <<'EOF'
#!/bin/sh
# fake Store backend: logs its arguments; FAKE_FAIL=1 answers as for an unreachable registry
printf '%s\n' "$*" >> "$FAKE_LOG"
if [ "${FAKE_FAIL:-0}" = 1 ]; then printf 'ERROR\tregistry unavailable\t%s\n' "$2"; exit 1; fi
printf '2026-10-11 INFO sync\nREGISTRY\tUPDATED\t%s\t%s\tok\t9\t%s\n' "$2" "$3" "$5"
EOF
chmod +x "$FAKE"
# run default-sources.sh; output in $OUT, exit code in $RC; state folder $S
seed() { OUT=$(HOME="$TMP/home" M5APPSTORE_STATE_DIR="$S" APPLAUNCH_APPSTORE_BIN="${BIN:-$FAKE}" FAKE_LOG="$LOG" \
                FAKE_FAIL="${SYNC_FAIL:-0}" sh "$SEED" "$@" 2>&1); RC=$?; }
calls() { if [ -f "$LOG" ]; then wc -l < "$LOG" | tr -d ' '; else echo 0; fi; }
json_ok() { if command -v python3 >/dev/null 2>&1; then python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$1"; else true; fi; }
HUB='    {
      "builtin": true,
      "enabled": true,
      "name": "CardputerZero Hub",
      "region": "default",
      "url": "https://cardputerzero.github.io/generated/registry.json"
    }'
HUB_ONLY="{
  \"active_region\": \"default\",
  \"region\": \"auto\",
  \"registries\": [
$HUB
  ]
}"
EXPECT_SEED="{
  \"active_region\": \"default\",
  \"region\": \"auto\",
  \"registries\": [
$HUB,
    {
      \"enabled\": true,
      \"name\": \"OSRdesign/cyberdeck-zero-apps\",
      \"url\": \"$SRC_URL\"
    }
  ]
}"

# fresh install: no state folder at all
S="$TMP/state-fresh"; rm -f "$LOG"
seed --dry-run
check "seed dry-run: plan and JSON, nothing written" '[ $RC = 0 ] && has "would add OSRdesign/cyberdeck-zero-apps" && has "$SRC_URL" && [ ! -e "$S" ] && [ "$(calls)" = 0 ]'
seed
check "seed fresh: exit 0, added and synced" '[ $RC = 0 ] && has "added OSRdesign/cyberdeck-zero-apps" && has "synced: 9 apps (ok)"'
check "seed fresh: the Store format exactly" '[ "$(cat "$S/registries.json")" = "$EXPECT_SEED" ]'
check "seed fresh: valid JSON" 'json_ok "$S/registries.json"'
check "seed fresh: one sync of that source only" '[ "$(cat "$LOG")" = "--edit-registry $SRC_URL $SRC_URL --registry-name OSRdesign/cyberdeck-zero-apps" ]'
check "seed fresh: marker written, no backup" '[ -f "$S/applaunch-default-source" ] && [ ! -e "$S/registries.json.bak-applaunch" ]'
SUM=$(cksum < "$S/registries.json")
seed
check "seed second run: decided already, nothing changed, no sync" '[ $RC = 0 ] && has "already decided" && [ "$(cksum < "$S/registries.json")" = "$SUM" ] && [ "$(calls)" = 1 ]'
# the user removed the source afterwards (the Store then keeps the hub only): a re-run does not add it back
printf '%s\n' "$HUB_ONLY" > "$S/registries.json"
seed
check "seed after the user removed it: not added back" 'has "already decided" && [ "$(cat "$S/registries.json")" = "$HUB_ONLY" ]'

# existing install whose list holds the built-in hub only (region China): seeded, region kept, old file backed up
S="$TMP/state-hub"; rm -f "$LOG"; mkdir -p "$S"
printf '%s\n' "$HUB_ONLY" | sed -e 's/"active_region": "default"/"active_region": "CN"/' -e 's/"region": "auto"/"region": "CN"/' \
    -e 's/"name": "CardputerZero Hub"/"name": "CardputerZero Hub CN"/' -e 's/"region": "default"/"region": "CN"/' \
    -e 's|https://cardputerzero.github.io/generated/registry.json|https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json|' \
    > "$S/registries.json"
BEFORE=$(cat "$S/registries.json")
seed --no-sync
check "seed hub only: added, not synced (--no-sync)" '[ $RC = 0 ] && has "added OSRdesign" && has "not synced (--no-sync)" && [ "$(calls)" = 0 ]'
check "seed hub only: CN region and hub kept" 'grep -q "^  \"region\": \"CN\",$" "$S/registries.json" && grep -q "^  \"active_region\": \"CN\",$" "$S/registries.json" && grep -q "Hub CN" "$S/registries.json" && grep -q "$SRC_URL" "$S/registries.json"'
check "seed hub only: valid JSON, backup of the old file" 'json_ok "$S/registries.json" && [ "$(cat "$S/registries.json.bak-applaunch")" = "$BEFORE" ]'

# the user's own list (another source): never changed, decision recorded, no sync
S="$TMP/state-own"; rm -f "$LOG"; mkdir -p "$S"
cat > "$S/registries.json" <<EOF
{
  "active_region": "default",
  "region": "auto",
  "registries": [
$HUB,
    {
      "enabled": false,
      "name": "me/my-apps",
      "url": "https://raw.githubusercontent.com/me/my-apps/main/registry.json"
    }
  ]
}
EOF
SUM=$(cksum < "$S/registries.json")
seed
check "seed own list: kept as is, no sync" '[ $RC = 0 ] && has "own sources (1) are kept" && [ "$(cksum < "$S/registries.json")" = "$SUM" ] && [ "$(calls)" = 0 ] && [ -f "$S/applaunch-default-source" ]'

# the source is already there (added by hand, disabled): left alone
S="$TMP/state-ours"; rm -f "$LOG"; mkdir -p "$S"
printf '{"registries":[{"url":"%s","name":"OSRdesign/cyberdeck-zero-apps","enabled":false}]}\n' "$SRC_URL" > "$S/registries.json"
SUM=$(cksum < "$S/registries.json")
seed
check "seed already listed: left alone" '[ $RC = 0 ] && has "already in the list" && [ "$(cksum < "$S/registries.json")" = "$SUM" ] && [ "$(calls)" = 0 ]'

# sync failures never fail the install; the list stays written
S="$TMP/state-offline"; rm -f "$LOG"
SYNC_FAIL=1; seed; SYNC_FAIL=0
check "seed offline: exit 0, reason shown, list written" '[ $RC = 0 ] && has "could not sync it now (registry unavailable)" && [ "$(cat "$S/registries.json")" = "$EXPECT_SEED" ]'
S="$TMP/state-nostore"; rm -f "$LOG"
BIN="$TMP/no-such-appstore"; seed; BIN=""
check "seed without the Store: list written, not synced" '[ $RC = 0 ] && has "is not installed: not synced" && [ -f "$S/registries.json" ]'
seed --bogus
check "seed unknown option: exit 2" '[ $RC = 2 ]'

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
