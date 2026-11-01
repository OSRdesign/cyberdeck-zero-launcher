#!/bin/sh
# PC test of Settings > Updates' root helper (cyberdeckzero-update, report 047 P2) and of what install.sh gives it
# (install.conf, --plan), in a fake root: UPDATE_ROOT + UPDATE_FAKE_SYSTEM=1 for the helper, SYSROOT +
# INSTALL_FAKE_SYSTEM=1 for install.sh, and stand-ins for systemctl, runuser, systemd-inhibit, dpkg, apt-get and the other
# system commands first on PATH. The "launcher" of a fake bundle is a text file naming its version: the stand-ins start
# it by writing board.state with that version and the pid of a live sleep process (a "broken" launcher writes nothing),
# so the health check, the rollback and both restart paths run as on a board. Changes nothing outside a temp dir, needs
# no root and no Pi (the helper refuses to run without root unless it is pointed at a fake root).
#   sh projects/APPLaunch/pizero2w/tests/test-update-helper.sh
set -u
if [ "$(id -u)" = 0 ]; then
    echo "run it as a normal user: as root a mistake could reach the live system" >&2
    exit 2
fi
HERE=$(cd "$(dirname "$0")" && pwd)
D="$HERE/.."
FIX="$HERE/fixtures"
TMP=$(mktemp -d)
cleanup() { [ -f "$TMP/sleeps" ] && while read -r p; do kill "$p" 2>/dev/null; done < "$TMP/sleeps"; rm -rf "$TMP"; }
trap cleanup EXIT
export INSTALL_GLIBC_VERSION=2.41
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); printf 'ok   %s\n' "$1"; }
ko() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$1"; [ -z "${2:-}" ] || printf '%s\n' "$2" | sed 's/^/     | /'; }
has() { case "$OUT" in *"$1"*) return 0 ;; esac; return 1; }
check() { if eval "$2"; then ok "$1"; else ko "$1" "$OUT
--- status:
$(cat "$R/var/lib/cyberdeckzero-update/status" 2>/dev/null)
--- log tail:
$(tail -n 25 "$R"/var/log/cyberdeckzero/update-*.log 2>/dev/null | tail -n 25)"; fi; }
U=$(id -un)
UID_=$(id -u)
UHOME=$(getent passwd "$U" | cut -d: -f6)
# a live process standing for a running launcher (killed at the end)
live() { sleep 600 >/dev/null 2>&1 & echo "$!" >> "$TMP/sleeps"; echo "$!"; }

# --- two fake bundles as build.sh assembles them: the "launcher" names its version
bundle() { # bundle DIR VERSION|broken
    b="$1/bundle"; rm -rf "$1"; mkdir -p "$b/payload/bin" "$b/payload/lib" "$b/payload/etc" "$b/payload/share/share/images" \
        "$b/payload/doc"
    cp "$D/install.sh" "$D/uninstall.sh" "$D/default-sources.sh" "$b/"
    printf '#!/bin/sh\n# version %s\necho launcher\n' "$2" > "$b/payload/bin/cyberdeckzero"
    cp "$D/cyberdeckzero-console" "$D/cyberdeckzero-update" "$b/payload/bin/"
    chmod 755 "$b/payload/bin/"*
    : > "$b/payload/lib/libapplaunch_vfb.so"
    for f in cyberdeckzero.service launcher-ntp-default.service waveshare-pwm-backlight.dts hyperpixel4-pwm-backlight.dts \
             90-backlight-unblank.rules 91-applaunch-vkbd.rules 93-applaunch-rfkill.rules 50-networkmanager-netdev.rules \
             51-launcher-time-power.rules cyberdeckzero-console.service cyberdeckzero-desktop.service \
             52-cyberdeckzero-desktop.rules cyberdeckzero.desktop; do
        cp "$D/$f" "$b/payload/etc/"
    done
    echo "png $2" > "$b/payload/share/share/images/setting_100.png"
    ( cd "$b/payload/share" && find . -type f | sed 's|^\./||' | LC_ALL=C sort ) > "$b/payload/share.manifest"
    echo "notes $2" > "$b/payload/doc/release-notes-$2.md"
}
tarball() { # tarball BUNDLE_PARENT OUT.tar.gz
    tar -C "$1" -czf "$2" bundle
}
bundle "$TMP/old" 0.8.12
bundle "$TMP/new" 0.8.13
bundle "$TMP/broken" broken
# the old release ships Store images and an icon that the new install.sh retires (RETIRED_FILES); the broken release
# ships a file of its own in share/ (a rollback removes it again, like a new file in bin/ or lib/)
printf 'store image of 0.8.12\000\377 binary\n' > "$TMP/old/bundle/payload/share/share/images/store_100.png"
printf 'tank icon of 0.8.12\n' > "$TMP/old/bundle/payload/share/share/images/tank.png"
# ... and the two large fonts the new bundle drops (report 048 section 3.3), next to a font that stays
mkdir -p "$TMP/old/bundle/payload/share/share/font"
printf 'serif font of 0.8.12\000\377 binary\n' > "$TMP/old/bundle/payload/share/share/font/NotoSerifCJK-Regular.ttc"
printf 'thin font of 0.8.12\000\376 binary\n' > "$TMP/old/bundle/payload/share/share/font/AlibabaPuHuiTi-3-35-Thin.ttf"
printf 'sans font of 0.8.12\n' > "$TMP/old/bundle/payload/share/share/font/NotoSansCJK-Regular.ttc"
# (the old release's own install.sh does not retire them: only the new one does)
sed -i -e 's#share/images/store_100.png##' -e 's#share/images/tank.png##' -e 's#share/font/NotoSerifCJK-Regular.ttc##' \
    -e 's#share/font/AlibabaPuHuiTi-3-35-Thin.ttf##' "$TMP/old/bundle/install.sh"
( cd "$TMP/old/bundle/payload/share" && find . -type f | sed 's|^\./||' | LC_ALL=C sort ) > "$TMP/old/bundle/payload/share.manifest"
echo "only in the broken release" > "$TMP/broken/bundle/payload/share/share/images/broken_only.png"
NEWTGZ="$TMP/cyberdeckzero-launcher-0.8.13-arm64.tar.gz"; tarball "$TMP/new" "$NEWTGZ"
BROKENTGZ="$TMP/cyberdeckzero-launcher-0.8.14-arm64.tar.gz"; tarball "$TMP/broken" "$BROKENTGZ"
sha() { sha256sum "$1" | cut -d' ' -f1; }

# --- stand-ins for the system commands (calls logged in $FAKE_CALLS)
FAKEBIN="$TMP/fakebin"; CALLS="$TMP/calls.log"
mkdir -p "$FAKEBIN"
# the launcher an install put in the fake root starts: board.state names its version and a live pid ("broken": none)
cat > "$FAKEBIN/fake-launcher-start" <<'EOF'
#!/bin/sh
r=${UPDATE_ROOT:-${SYSROOT:-}}
v=$(sed -n 's/^# version //p' "$r/usr/share/APPLaunch/bin/cyberdeckzero" 2>/dev/null)
s="$r/run/user/$(id -u)/applaunch"
mkdir -p "$s"
if [ -n "$v" ] && [ "$v" != broken ]; then
    printf 'v=1\ngen=1\npid=%s\nlauncher=%s\n' "$FAKE_LAUNCHER_PID" "$v" > "$s/board.state"
else
    printf 'v=1\ngen=1\npid=999999999\nlauncher=%s\n' "$v" > "$s/board.state"
fi
EOF
cat > "$FAKEBIN/systemctl" <<'EOF'
#!/bin/sh
# stand-in systemctl: logs; is-active from FAKE_ACTIVE; starting the desktop unit starts the installed launcher
printf 'systemctl %s\n' "$*" >> "$FAKE_CALLS"
case " $* " in
    *" is-active "*) for a in "$@"; do [ -n "$a" ] || continue; case " ${FAKE_ACTIVE:-} " in *" $a "*) exit 0 ;; esac; done; exit 3 ;;
    *" cat "*) exit 1 ;;
    *" start --no-block cyberdeckzero-desktop.service "*) fake-launcher-start ;;
esac
exit 0
EOF
cat > "$FAKEBIN/runuser" <<'EOF'
#!/bin/sh
# stand-in runuser: logs; "systemctl --user restart cyberdeckzero.service" starts the installed launcher
printf 'runuser %s\n' "$*" >> "$FAKE_CALLS"
# default-sources.sh: what the user could read at this moment (path, mode of the file and its folder, its content)
for last in "$@"; do :; done
case "$last" in */default-sources.sh)
    printf 'path=%s\nmode=%s\ndirmode=%s\n' "$last" "$(stat -c %a "$last" 2>/dev/null)" "$(stat -c %a "${last%/*}" 2>/dev/null)" > "$FAKE_CALLS.ds"
    cat "$last" > "$FAKE_CALLS.ds.content" 2>/dev/null ;;
esac
case " $* " in
    *" is-active "*) for a in "$@"; do [ -n "$a" ] || continue; case " ${FAKE_USER_ACTIVE:-} " in *" $a "*) exit 0 ;; esac; done; exit 3 ;;
    *" --user restart cyberdeckzero.service"*) fake-launcher-start ;;
esac
exit 0
EOF
cat > "$FAKEBIN/systemd-inhibit" <<'EOF'
#!/bin/sh
# stand-in systemd-inhibit: logs its options, runs the command; FAKE_KILL_HELPER=1: then kills the helper (a power cut)
printf 'systemd-inhibit %s\n' "$*" >> "$FAKE_CALLS"
while [ $# -gt 0 ]; do case "$1" in --*) shift ;; *) break ;; esac; done
"$@"; rc=$?
if [ "${FAKE_KILL_HELPER:-0}" = 1 ]; then kill -9 "$PPID"; fi
exit "$rc"
EOF
printf '#!/bin/sh\necho "${FAKE_UNAME_M:-aarch64}"\n' > "$FAKEBIN/uname"
printf '#!/bin/sh\nprintf "apt-get %%s\\n" "$*" >> "$FAKE_CALLS"\nexit "${FAKE_APT_RC:-1}"\n' > "$FAKEBIN/apt-get"
cat > "$FAKEBIN/dpkg" <<'EOF'
#!/bin/sh
printf 'dpkg %s\n' "$*" >> "$FAKE_CALLS"
if [ "$1" = -s ]; then case " ${FAKE_DPKG_MISSING:-} " in *" $2 "*) exit 1 ;; esac; fi
exit 0
EOF
printf '#!/bin/sh\nprintf "pkaction %%s\\n" "$*" >> "$FAKE_CALLS"\nexit 0\n' > "$FAKEBIN/pkaction"
cat > "$FAKEBIN/sudo" <<'EOF'
#!/bin/sh
# stand-in sudo (uninstall.sh): logs the call and runs it as the caller
printf 'sudo %s\n' "$*" >> "$FAKE_CALLS"
[ "$1" = -v ] && exit 0
exec "$@"
EOF
# stand-in df: FAKE_DF_KIB set = that many KiB free on one file system, else the real df
cat > "$FAKEBIN/df" <<'EOF'
#!/bin/sh
if [ -n "${FAKE_DF_KIB:-}" ]; then
    printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\nfakefs 99999999 0 %s 0%% /\n' "$FAKE_DF_KIB"
    exit 0
fi
for r in /bin/df /usr/bin/df; do [ -x "$r" ] && exec "$r" "$@"; done
exit 1
EOF
for c in loginctl udevadm usermod chown dtc groupadd; do
    printf '#!/bin/sh\nprintf "%s %%s\\n" "$*" >> "$FAKE_CALLS"\nexit 0\n' "$c" > "$FAKEBIN/$c"
done
chmod 755 "$FAKEBIN"/*
export FAKE_CALLS="$CALLS"
called() { grep -q -F -- "$1" "$CALLS"; }
at() { n=$(grep -n -F -- "$1" "$CALLS" | head -n 1 | cut -d: -f1); echo "${n:-0}"; }

# a fresh fake root of a board fixture
root() { rm -rf "$TMP/r-$1"; cp -R "$FIX/$1" "$TMP/r-$1"; echo "$TMP/r-$1"; }
# install.sh of the old bundle into $R as a first install would run it
first_install() {
    : > "$CALLS"
    OUT=$(PATH="$FAKEBIN:$PATH" SYSROOT="$R" INSTALL_FAKE_SYSTEM=1 FAKE_LAUNCHER_PID="$LPID" \
          sh "$TMP/old/bundle/install.sh" --user "$U" "$@" 2>&1); RC=$?
}
# the download as the launcher leaves it: in the user's cache, owned by the user
download() { # download TARBALL
    mkdir -p "$R$UHOME/.cache/cyberdeckzero/update"
    cp "$1" "$R$UHOME/.cache/cyberdeckzero/update/"
    FILE="$UHOME/.cache/cyberdeckzero/update/${1##*/}"
}
# helper ARGS...: the helper the install put in the fake root (the one a board runs; HELPER: another one), with short
# waits (HT: the health timeout). FAKE_* switches for the stand-ins are exported by the caller and unset after.
HT=4
HELPER=""
helper() {
    : > "$CALLS"
    OUT=$(PATH="$FAKEBIN:$PATH" UPDATE_ROOT="$R" UPDATE_FAKE_SYSTEM=1 UPDATE_HEALTH_TIMEOUT="$HT" UPDATE_HEALTH_STABLE=1 \
          UPDATE_RESTART_WAIT=3 UPDATE_DESKTOP_GAP=0 FAKE_LAUNCHER_PID="$LPID" \
          sh "${HELPER:-$R/usr/share/APPLaunch/bin/cyberdeckzero-update}" "$@" 2>&1); RC=$?
}
apply() { # apply TARBALL VERSION [extra]
    helper apply --version "$2" --from-version 0.8.12 --sha256 "$(sha "$1")" --file "$FILE" --user "$U"
}
st() { sed -n "s/^$1=//p" "$R/var/lib/cyberdeckzero-update/status" | tail -n 1; }
bin_version() { sed -n 's/^# version //p' "$R/usr/share/APPLaunch/bin/cyberdeckzero"; }
running() { sed -n 's/^launcher=//p' "$R/run/user/$UID_/applaunch/board.state" 2>/dev/null; }
# the system part of the fake root without the helper's own folders and the runtime state
snapshot() { ( cd "$R" && find . \( -type f -o -type l \) ! -path './var/lib/cyberdeckzero-update/*' ! -path './var/log/*' \
    ! -path './run/*' ! -path './var/cache/*' ! -path "./${UHOME#/}/.cache/*" | LC_ALL=C sort | while IFS= read -r f; do
    if [ -L "$f" ]; then echo "$f -> $(readlink "$f")"; else echo "$f $(cksum < "$f")"; fi; done ); }

# ------------------------------------------------------------------------------------------------------------------
# safety: never the live system (this test refuses to run as root, see the top)
OUT=$(sh "$D/cyberdeckzero-update" apply --version 0.8.13 --from-version 0.8.12 --sha256 "$(sha "$NEWTGZ")" --file /x --user "$U" 2>&1); RC=$?
check "helper without root and without a fake root: refused, exit 2" '[ $RC = 2 ] && has "run it as root"'
OUT=$(UPDATE_ROOT="$TMP/none" sh "$D/cyberdeckzero-update" status 2>&1); RC=$?
check "UPDATE_ROOT without UPDATE_FAKE_SYSTEM=1: refused" '[ $RC = 2 ] && has "test hook"'
OUT=$(UPDATE_FAKE_SYSTEM=1 sh "$D/cyberdeckzero-update" status 2>&1); RC=$?
check "UPDATE_FAKE_SYSTEM=1 without UPDATE_ROOT: refused" '[ $RC = 2 ] && has "needs UPDATE_ROOT"'

# ------------------------------------------------------------------------------------------------------------------
# install.sh: the options recorded, the plan for a program (the PDA Pi 3A+: --board pi3a-wsdsi43-portrait)
LPID=$(live)
R=$(root pi3a-wsdsi43)
mkdir -p "$R/boot/firmware"
printf 'dtoverlay=vc4-kms-v3d\ndtoverlay=vc4-kms-dsi-waveshare-800x480\n' > "$R/boot/firmware/config.txt"
echo 'console=tty1 root=PARTUUID=1-02 rootwait' > "$R/boot/firmware/cmdline.txt"
first_install --keyboard-name "Keychron K3 Pro" --board pi3a-wsdsi43-portrait --no-boot-config --no-default-source
C="$R/etc/applaunch/install.conf"
check "first install: exit 0, the helper installed (755) next to the launcher" '[ $RC = 0 ] && [ -x "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" ] && [ "$(stat -c %a "$R/usr/share/APPLaunch/bin/cyberdeckzero-update")" = 755 ]'
check "first install: install.conf records the options (644)" '[ "$(stat -c %a "$C")" = 644 ] && grep -qx "keyboard_name=Keychron K3 Pro" "$C" && grep -qx "board=pi3a-wsdsi43-portrait" "$C" && grep -qx "boot_config=0" "$C" && grep -qx "pwm_backlight=0" "$C" && grep -qx "default_source=0" "$C" && grep -qx "user=$U" "$C"'
check "first install: the step says so" 'has "== Install options" && has "/etc/applaunch/install.conf: keyboard \"Keychron K3 Pro\", --board pi3a-wsdsi43-portrait, --no-boot-config, --no-default-source"'
check "first install: cmdline.txt untouched (--no-boot-config)" '[ "$(cat "$R/boot/firmware/cmdline.txt")" = "console=tty1 root=PARTUUID=1-02 rootwait" ]'
OUT=$(SYSROOT="$R" sh "$TMP/new/bundle/install.sh" --plan --user "$U" --board pi3a-wsdsi43-portrait 2>&1); RC=$?
check "install.sh --plan: key=value lines only, exit 0" '[ $RC = 0 ] && [ -z "$(printf "%s\n" "$OUT" | grep -v "^[a-z_]*=")" ]'
check "install.sh --plan: board, session, no switch, no migration, the profile kept" 'has "plan=1" && has "board=pi3a-wsdsi43-portrait" && has "board_forced=1" && has "session=appliance" && has "switch=
" && has "migration=0" && has "profile_action=keep" && has "profile_state=ours" && has "desktop_refused=0" && has "user=$U"'
OUT=$(SYSROOT="$R" sh "$TMP/new/bundle/install.sh" --dry-run --user "$U" 2>&1)
check "install.sh --dry-run names install.conf" 'has "install options: /etc/applaunch/install.conf"'
OUT=$(sh "$TMP/new/bundle/install.sh" --help 2>&1)
check "install.sh --help: --plan and install.conf described" 'has "--plan" && has "/etc/applaunch/install.conf"'

# ------------------------------------------------------------------------------------------------------------------
# apply, appliance board: the options replayed, a backup, the launcher restarted by install.sh, healthy: done
CONF_SUM=$(cksum < "$R/etc/applaunch/board.conf")
download "$NEWTGZ"
apply "$NEWTGZ" 0.8.13
check "apply (appliance): exit 0, state done, the new launcher installed and running" '[ $RC = 0 ] && [ "$(st state)" = done ] && [ "$(st to)" = 0.8.13 ] && [ "$(st from)" = 0.8.12 ] && [ "$(st variant)" = appliance ] && [ "$(bin_version)" = 0.8.13 ] && [ "$(running)" = 0.8.13 ]'
check "apply: the options of the first install replayed (keyboard name, --board, --no-boot-config, --no-default-source), never --force" 'grep -q -- "  --user $U --keyboard-name \"Keychron K3 Pro\" --board pi3a-wsdsi43-portrait --no-boot-config --no-default-source$" "$R/var/log/cyberdeckzero/"update-*.log && grep -q "recorded in /etc/applaunch/install.conf" "$R/var/log/cyberdeckzero/"update-*.log && grep -q "Keychron K3 Pro" "$R/etc/udev/rules.d/99-bt-keyboard.rules" && grep -qx "board=pi3a-wsdsi43-portrait" "$C" && ! grep -q -- "--force" "$CALLS"'
check "apply: board.conf and cmdline.txt unchanged" '[ "$(cksum < "$R/etc/applaunch/board.conf")" = "$CONF_SUM" ] && [ "$(cat "$R/boot/firmware/cmdline.txt")" = "console=tty1 root=PARTUUID=1-02 rootwait" ]'
check "apply: install.sh restarted the user service, the helper did not restart it again" '[ "$(grep -c -- "--user restart cyberdeckzero.service" "$CALLS")" = 1 ]'
check "apply: install.sh ran under systemd-inhibit (sleep, idle, shutdown, power key)" 'called "systemd-inhibit --what=sleep:idle:shutdown:handle-power-key:handle-lid-switch"'
B="$R/var/lib/cyberdeckzero-update/backup"
check "apply: the backup holds the old launcher, its units, rules, board.conf and install.conf" '[ "$(sed -n "s/^# version //p" "$B/files/usr/share/APPLaunch/bin/cyberdeckzero")" = 0.8.12 ] && [ -f "$B/files$UHOME/.config/systemd/user/cyberdeckzero.service" ] && [ -f "$B/files/etc/udev/rules.d/99-bt-keyboard.rules" ] && [ -f "$B/files/etc/applaunch/board.conf" ] && [ -f "$B/files/etc/applaunch/install.conf" ] && grep -qx "dir /usr/share/APPLaunch/bin" "$B/manifest" && grep -qx "absent /etc/systemd/system/cyberdeckzero-desktop.service" "$B/manifest"'
check "apply: the backup is root-only (700), the work folder is gone, the status and the log are world-readable" '[ "$(stat -c %a "$B")" = 700 ] && [ ! -e "$R/var/cache/cyberdeckzero-update" ] && [ "$(stat -c %a "$R/var/lib/cyberdeckzero-update/status")" = 644 ] && [ "$(stat -c %a "$R"/var/log/cyberdeckzero/update-*.log)" = 644 ]'
check "apply: it ran from its copy in /run (install.sh replaces the installed file)" '[ -f "$R/run/cyberdeckzero-update/cyberdeckzero-update.running" ] && [ "$(st log)" = "$(ls "$R"/var/log/cyberdeckzero/update-*.log | sed "s|^$R||")" ]'
check "apply: the new release's notes and the new helper are installed" '[ -f "$R/usr/share/doc/cyberdeckzero-launcher/release-notes-0.8.13.md" ] && [ -x "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" ]'
check "status: the helper prints its status file" 'helper status; has "state=done" && has "step=health"'

# ------------------------------------------------------------------------------------------------------------------
# the health check times out (a launcher that never starts): the backup back, the old launcher restarted
sleep 1   # a log of its own (the names carry the second)
download "$BROKENTGZ"
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(sha "$BROKENTGZ")" --file "$FILE" --user "$U"
check "broken release: exit 1, state rolled-back, the reason" '[ $RC = 1 ] && [ "$(st state)" = rolled-back ] && [ "$(st reason)" = "0.8.14 did not start" ] && [ "$(st backup)" = 1 ]'
check "broken release: 0.8.13 restored and restarted (user service), running again" '[ "$(bin_version)" = 0.8.13 ] && [ "$(running)" = 0.8.13 ] && [ "$(grep -c -- "--user restart cyberdeckzero.service" "$CALLS")" = 2 ] && called "--user reset-failed cyberdeckzero.service"'
check "broken release: the 0.8.14 notes are gone again (the doc folder restored whole)" '[ ! -e "$R/usr/share/doc/cyberdeckzero-launcher/release-notes-0.8.14.md" ] && [ -f "$R/usr/share/doc/cyberdeckzero-launcher/release-notes-0.8.13.md" ]'
check "broken release: the log tells the story" 'grep -q "rolling back: 0.8.14 did not start" "$(ls -t "$R"/var/log/cyberdeckzero/update-*.log | head -n 1)" && grep -q "backup of 0.8.13 restored" "$(ls -t "$R"/var/log/cyberdeckzero/update-*.log | head -n 1)"'
check "broken release: the log ends with the explicit last line (the unit fails on purpose, exit 1)" '[ "$(tail -n 1 "$(ls -t "$R"/var/log/cyberdeckzero/update-*.log | head -n 1)" | cut -d" " -f2-)" = "rolled back to 0.8.13: 0.8.14 did not start" ]'

# install.sh itself fails (apt cannot install a missing package): rolled back, the running old launcher left alone
sleep 1
download "$BROKENTGZ"
FAKE_DPKG_MISSING=curl; FAKE_APT_RC=100; export FAKE_DPKG_MISSING FAKE_APT_RC
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(sha "$BROKENTGZ")" --file "$FILE" --user "$U"
unset FAKE_DPKG_MISSING FAKE_APT_RC
check "install.sh fails: exit 1, rolled back with the exit status, nothing restarted (0.8.13 still runs)" '[ $RC = 1 ] && [ "$(st state)" = rolled-back ] && [ "$(st reason)" = "install.sh failed (exit status 100)" ] && [ "$(bin_version)" = 0.8.13 ] && ! called "restart cyberdeckzero.service" && grep -q "launcher 0.8.13 still runs" "$(ls -t "$R"/var/log/cyberdeckzero/update-*.log | head -n 1)"'

# refusals: nothing changes (the hash, the plan: a variant switch), the arguments
SNAP=$(snapshot)
download "$NEWTGZ"
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(printf '%064d' 0)" --file "$FILE" --user "$U"
check "a checksum mismatch: refused (exit 2), nothing changed" '[ $RC = 2 ] && [ "$(st state)" = refused ] && [ "$(st reason)" = "the download does not match its checksum" ] && [ "$(snapshot)" = "$SNAP" ]'
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 nothex --file "$FILE" --user "$U"
check "bad arguments: exit 2, the usage, no status written" '[ $RC = 2 ] && has "apply needs" && [ "$(st reason)" = "the download does not match its checksum" ]'
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(sha "$NEWTGZ")" --file "/etc/passwd" --user "$U"
check "a file outside the user's home: refused" '[ $RC = 2 ] && [ "$(st reason)" = "the download is not in the home folder of $U" ]'
ln -sf /etc/hostname "$R$UHOME/.cache/cyberdeckzero/update/link.tar.gz"
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(sha "$NEWTGZ")" --file "$UHOME/.cache/cyberdeckzero/update/link.tar.gz" --user "$U"
check "a symlink as the download: refused" '[ $RC = 2 ] && [ "$(st reason)" = "the download is missing or not a plain file" ] && [ "$(snapshot)" = "$SNAP" ]'
# a desktop was installed meanwhile: the plan switches the variant
mkdir -p "$R/etc/systemd/system" "$R/lib/systemd/system" "$R/proc/701"
ln -sf /lib/systemd/system/lightdm.service "$R/etc/systemd/system/display-manager.service"
echo "[Unit]" > "$R/lib/systemd/system/lightdm.service"; echo lightdm > "$R/proc/701/comm"
SNAP=$(snapshot)
helper apply --version 0.8.13 --from-version 0.8.13 --sha256 "$(sha "$NEWTGZ")" --file "$FILE" --user "$U"
check "a variant switch planned: refused (exit 2), needs install.sh by hand, nothing changed" '[ $RC = 2 ] && [ "$(st state)" = refused ] && [ "$(st reason)" = "needs install.sh by hand: a desktop was installed: the launcher would move to the desktop variant" ] && [ "$(snapshot)" = "$SNAP" ] && ! called "systemd-inhibit"'
OUT=$(PATH="$FAKEBIN:$PATH" UPDATE_ROOT="$R" UPDATE_FAKE_SYSTEM=1 sh "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" plan --bundle "$TMP/new/bundle" --user "$U" 2>&1); RC=$?
check "plan: the options and the refusal, exit 2" '[ $RC = 2 ] && has "--keyboard-name \"Keychron K3 Pro\" --board pi3a-wsdsi43-portrait" && has "switch=to-desktop" && has "result: refused: needs install.sh by hand: a desktop was installed"'
rm -rf "$R/etc/systemd/system/display-manager.service" "$R/proc/701"

# ------------------------------------------------------------------------------------------------------------------
# a board installed before install.conf: the options inferred (the Pi 4 B's hand-edited PDA profile, a keyboard name,
# no boot configuration, no default source), board.conf kept byte for byte
R=$(root pi4b-dsi)
mkdir -p "$R/boot/firmware"
printf 'display_auto_detect=1\ndtoverlay=vc4-kms-v3d\n' > "$R/boot/firmware/config.txt"
echo 'console=tty1 root=PARTUUID=2-02 rootwait' > "$R/boot/firmware/cmdline.txt"
first_install --keyboard-name "Old Keys" --board pi3a-wsdsi43-portrait --no-boot-config --no-default-source
sed -i 's/^APPLAUNCH_PANEL_MM=.*/APPLAUNCH_PANEL_MM=108x65/' "$R/etc/applaunch/board.conf"   # the owner's hand edit
rm -f "$R/etc/applaunch/install.conf"
HAND=$(cksum < "$R/etc/applaunch/board.conf")
helper options --user "$U"
check "options inferred: keyboard from 99-bt-keyboard.rules, --board from board.conf, --no-boot-config, --no-default-source" '[ $RC = 0 ] && has "inferred" && has "--user $U --keyboard-name \"Old Keys\" --board pi3a-wsdsi43-portrait --no-boot-config --no-default-source"'
download "$NEWTGZ"
apply "$NEWTGZ" 0.8.13
check "inferred options: done, the hand-edited board.conf kept byte for byte, the keyboard name kept" '[ $RC = 0 ] && [ "$(st state)" = done ] && [ "$(cksum < "$R/etc/applaunch/board.conf")" = "$HAND" ] && grep -q "Old Keys" "$R/etc/udev/rules.d/99-bt-keyboard.rules" && [ "$(cat "$R/boot/firmware/cmdline.txt")" = "console=tty1 root=PARTUUID=2-02 rootwait" ]'
check "inferred options: install.conf written by the new install.sh with them" 'grep -qx "keyboard_name=Old Keys" "$R/etc/applaunch/install.conf" && grep -qx "board=pi3a-wsdsi43-portrait" "$R/etc/applaunch/install.conf" && grep -qx "boot_config=0" "$R/etc/applaunch/install.conf"'
# the deck (no board.conf), cmdline arguments there, a default source decided: nothing forced, boot config replayed
R=$(root zero2w)
mkdir -p "$R/boot/firmware/overlays" "$R$UHOME/.local/share/cardputerzero-appstore"
printf 'dtoverlay=waveshare-28dpi-3b\n' > "$R/boot/firmware/config.txt"
echo 'console=tty1 rootwait vt.global_cursor_default=0 consoleblank=0' > "$R/boot/firmware/cmdline.txt"
: > "$R$UHOME/.local/share/cardputerzero-appstore/applaunch-default-source"
HELPER="$D/cyberdeckzero-update"
helper options --user "$U"
HELPER=""
check "deck, inferred: the default keyboard name, no --board, boot config and default source on" 'has "--user $U --keyboard-name \"M4 Keyboard\"" && ! has "--board" && ! has "--no-boot-config" && ! has "--no-default-source"'

# the default app source step of an update: install.sh runs default-sources.sh as the user from a copy the user can read,
# never from the unpacked bundle (the helper's work folder is root-only, 0700: "sh: cannot open ...: Permission denied")
R=$(root pi3a-wsdsi43)
mkdir -p "$R/boot/firmware"
printf 'dtoverlay=vc4-kms-v3d\ndtoverlay=vc4-kms-dsi-waveshare-800x480\n' > "$R/boot/firmware/config.txt"
echo 'console=tty1 root=PARTUUID=1-02 rootwait' > "$R/boot/firmware/cmdline.txt"
first_install --keyboard-name "Keychron K3 Pro" --board pi3a-wsdsi43-portrait --no-boot-config
check "default source on: the first install runs default-sources.sh as the user" '[ $RC = 0 ] && grep -q "^runuser -u $U -- env HOME=.* sh .*/default-sources.sh" "$CALLS" && grep -qx "default_source=1" "$R/etc/applaunch/install.conf"'
rm -f "$CALLS.ds" "$CALLS.ds.content"
download "$NEWTGZ"
apply "$NEWTGZ" 0.8.13
check "default source on, update: done" '[ $RC = 0 ] && [ "$(st state)" = done ]'
check "update: default-sources.sh ran as the user" 'grep -q "^runuser -u $U -- env HOME=.* sh .*/default-sources.sh" "$CALLS" && [ -f "$CALLS.ds" ]'
DS_PATH=$(sed -n 's/^path=//p' "$CALLS.ds")
check "update: the script is not read from the work folder (root-only) nor the unpacked bundle" 'case "$DS_PATH" in */var/cache/cyberdeckzero-update/*|*/unpack/*|*/bundle/*) false ;; *) true ;; esac'
check "update: the user could read it (file 644, folder 755) and it is the bundle's script" '[ "$(sed -n "s/^mode=//p" "$CALLS.ds")" = 644 ] && [ "$(sed -n "s/^dirmode=//p" "$CALLS.ds")" = 755 ] && cmp -s "$CALLS.ds.content" "$D/default-sources.sh"'
check "update: the copy is removed again" '[ ! -e "$DS_PATH" ]'

# ------------------------------------------------------------------------------------------------------------------
# desktop board (HackBerry, the desktop unit): the running launcher exits with status 76 when asked
R=$(root hackberry-cm5)
mkdir -p "$R/etc/systemd/system" "$R/lib/systemd/system" "$R/proc/701" "$R/proc/702"
ln -sf /lib/systemd/system/lightdm.service "$R/etc/systemd/system/display-manager.service"
echo "[Unit]" > "$R/lib/systemd/system/lightdm.service"; echo lightdm > "$R/proc/701/comm"; echo labwc > "$R/proc/702/comm"
first_install --no-default-source
check "desktop board: the first install is the desktop variant" '[ $RC = 0 ] && [ -f "$R/etc/systemd/system/cyberdeckzero-desktop.service" ] && grep -qx "board=" "$R/etc/applaunch/install.conf"'
OLDPID=$(live)
mkdir -p "$R/run/user/$UID_/applaunch"
printf 'v=1\npid=%s\nlauncher=0.8.12\n' "$OLDPID" > "$R/run/user/$UID_/applaunch/board.state"
NEWPID=$(live)
# the old launcher: it sees restart=launcher in the status file, exits with 76, its unit starts the new one
( for i in $(seq 1 400); do
      if grep -qx "restart=launcher" "$R/var/lib/cyberdeckzero-update/status" 2>/dev/null; then
          grep -qx "launcher_pid=$OLDPID" "$R/var/lib/cyberdeckzero-update/status" || exit 0
          kill "$OLDPID"
          printf 'v=1\npid=%s\nlauncher=0.8.13\n' "$NEWPID" > "$R/run/user/$UID_/applaunch/board.state"
          exit 0
      fi
      sleep 0.1
  done ) &
WATCH=$!
download "$NEWTGZ"
FAKE_ACTIVE=cyberdeckzero-desktop.service; export FAKE_ACTIVE
apply "$NEWTGZ" 0.8.13
wait "$WATCH"
check "desktop, exit 76: done, the old launcher asked by its pid, no stop/start of the unit" '[ $RC = 0 ] && [ "$(st state)" = done ] && [ "$(st variant)" = desktop ] && [ "$(running)" = 0.8.13 ] && ! called "stop cyberdeckzero-desktop.service" && ! called "start --no-block cyberdeckzero-desktop.service" && grep -q "asked to exit with status 76" "$R"/var/log/cyberdeckzero/update-*.log'
check "desktop: install.sh did not restart anything itself, no user service touched" '! called "systemctl --user"'
# nobody answers (an app in front, the launcher frozen): after the wait, stop, the gap, start --no-block
sleep 1
OLDPID=$(live)
printf 'v=1\npid=%s\nlauncher=0.8.13\n' "$OLDPID" > "$R/run/user/$UID_/applaunch/board.state"
download "$NEWTGZ"
# (the 0.8.13 bundle again: the helper does not care, the launcher never offers it)
helper apply --version 0.8.13 --from-version 0.8.13 --sha256 "$(sha "$NEWTGZ")" --file "$FILE" --user "$U"
check "desktop, no answer: the unit stopped, then started with --no-block (never a plain restart), done" '[ $RC = 0 ] && [ "$(st state)" = done ] && called "systemctl stop cyberdeckzero-desktop.service" && [ "$(at "systemctl stop cyberdeckzero-desktop.service")" -lt "$(at "systemctl start --no-block cyberdeckzero-desktop.service")" ] && ! called "restart cyberdeckzero-desktop"'
# a broken release on the desktop board: rolled back, the old launcher brought back the same way
sleep 1
download "$BROKENTGZ"
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(sha "$BROKENTGZ")" --file "$FILE" --user "$U"
check "desktop, broken release: rolled back, 0.8.13 back through stop / start --no-block" '[ $RC = 1 ] && [ "$(st state)" = rolled-back ] && [ "$(bin_version)" = 0.8.13 ] && [ "$(running)" = 0.8.13 ] && [ "$(grep -c "start --no-block cyberdeckzero-desktop.service" "$CALLS")" = 2 ]'
# the launcher is not running (the desktop shows): installed, not checked
sleep 1
unset FAKE_ACTIVE
download "$NEWTGZ"
helper apply --version 0.8.13 --from-version 0.8.13 --sha256 "$(sha "$NEWTGZ")" --file "$FILE" --user "$U"
check "desktop, the launcher not running: done without a check, said so" '[ $RC = 0 ] && [ "$(st state)" = done ] && [ "$(st reason)" = "the launcher was not running: start it from the menu" ] && ! called "start --no-block"'

# ------------------------------------------------------------------------------------------------------------------
# an interruption (a power cut while install.sh ran): the status stays "running"; apply refuses, resume finishes it
R=$(root pi3a-luckfox35)
first_install --no-boot-config --no-default-source
download "$NEWTGZ"
FAKE_KILL_HELPER=1; export FAKE_KILL_HELPER
apply "$NEWTGZ" 0.8.13
unset FAKE_KILL_HELPER
check "interrupted after install.sh: the status says running at step install, with a backup" '[ "$(st state)" = running ] && [ "$(st step)" = install ] && [ "$(st backup)" = 1 ]'
sed -i 's/^boot=.*/boot=an-earlier-boot/' "$R/var/lib/cyberdeckzero-update/status"
apply "$NEWTGZ" 0.8.13
check "interrupted: a new apply is refused (repair first)" '[ $RC = 2 ] && has "interrupted: repair it first"'
helper resume --user "$U"
check "resume: the new launcher runs and stays up: done" '[ $RC = 0 ] && [ "$(st state)" = done ] && [ "$(st reason)" = "after an interruption" ] && [ "$(bin_version)" = 0.8.13 ]'
helper resume --user "$U"
check "resume with nothing to resume: exit 0" '[ $RC = 0 ] && has "nothing to resume"'
# interrupted, and the new launcher does not run: rolled back to the backup
download "$BROKENTGZ"
FAKE_KILL_HELPER=1; export FAKE_KILL_HELPER
helper apply --version 0.8.14 --from-version 0.8.13 --sha256 "$(sha "$BROKENTGZ")" --file "$FILE" --user "$U"
unset FAKE_KILL_HELPER
sed -i 's/^boot=.*/boot=an-earlier-boot/' "$R/var/lib/cyberdeckzero-update/status"
HT=2
helper resume --user "$U"
HT=4
check "resume, the new launcher not running: rolled back, 0.8.13 back and restarted" '[ $RC = 1 ] && [ "$(st state)" = rolled-back ] && [ "$(st reason)" = "the update was interrupted" ] && [ "$(bin_version)" = 0.8.13 ] && [ "$(running)" = 0.8.13 ]'
# interrupted before any change: nothing to repair
printf 'v=1\nstate=running\nstep=plan\nfrom=0.8.13\nto=0.8.14\nvariant=\nuser=%s\npid=999999999\nboot=x\nbackup=0\n' "$U" > "$R/var/lib/cyberdeckzero-update/status"
SNAP=$(snapshot)
helper resume --user "$U"
check "resume before any change: refused, nothing changed" '[ $RC = 2 ] && [ "$(st state)" = refused ] && [ "$(st reason)" = "interrupted before any change: nothing to repair" ] && [ "$(snapshot)" = "$SNAP" ]'

# ------------------------------------------------------------------------------------------------------------------
# the backup holds every file install.sh can delete or replace under /usr/share/APPLaunch (the images, audio, fonts:
# C2/C3 retire images): a rollback brings them back byte for byte
R=$(root pi3a-luckfox35)
first_install --no-boot-config --no-default-source
AR="$R/usr/share/APPLaunch"
SUM_STORE=$(cksum < "$AR/share/images/store_100.png"); SUM_TANK=$(cksum < "$AR/share/images/tank.png")
MODE_STORE=$(stat -c %a "$AR/share/images/store_100.png")
SUM_SERIF=$(cksum < "$AR/share/font/NotoSerifCJK-Regular.ttc"); SUM_THIN=$(cksum < "$AR/share/font/AlibabaPuHuiTi-3-35-Thin.ttf")
MODE_SERIF=$(stat -c %a "$AR/share/font/NotoSerifCJK-Regular.ttc")
SNAP_SHARE=$(cd "$AR/share" && find . -type f | LC_ALL=C sort | while IFS= read -r f; do echo "$f $(cksum < "$f")"; done)
check "old release installed: the two large fonts are there" '[ -f "$AR/share/font/NotoSerifCJK-Regular.ttc" ] && [ -f "$AR/share/font/AlibabaPuHuiTi-3-35-Thin.ttf" ] && [ -f "$AR/share/font/NotoSansCJK-Regular.ttc" ]'
check "old release installed: the retired images are there" '[ $RC = 0 ] && [ -f "$AR/share/images/store_100.png" ] && [ -f "$AR/share/images/tank.png" ]'
download "$BROKENTGZ"
HT=2
helper apply --version 0.8.14 --from-version 0.8.12 --sha256 "$(sha "$BROKENTGZ")" --file "$FILE" --user "$U"
HT=4
B="$R/var/lib/cyberdeckzero-update/backup"
check "failed health check: rolled back" '[ $RC = 1 ] && [ "$(st state)" = rolled-back ] && [ "$(bin_version)" = 0.8.12 ]'
check "the backup held the share images, the retired ones included, and the manifest names the folder" '[ -f "$B/files/usr/share/APPLaunch/share/images/store_100.png" ] && grep -qx "dir /usr/share/APPLaunch/share" "$B/manifest" && grep -qx "dir /usr/share/APPLaunch/bin" "$B/manifest"'
check "the new install.sh had retired them (the log says so)" 'grep -q "removed /usr/share/APPLaunch/share/images/store_100.png (no longer in the bundle)" "$R"/var/log/cyberdeckzero/update-*.log'
check "rollback: the retired Store image and tank icon are back, byte for byte, same mode" '[ -f "$AR/share/images/store_100.png" ] && [ "$(cksum < "$AR/share/images/store_100.png")" = "$SUM_STORE" ] && [ "$(cksum < "$AR/share/images/tank.png")" = "$SUM_TANK" ] && [ "$(stat -c %a "$AR/share/images/store_100.png")" = "$MODE_STORE" ]'
check "the new install.sh had retired the two large fonts (the log says so)" 'grep -q "removed /usr/share/APPLaunch/share/font/NotoSerifCJK-Regular.ttc (no longer in the bundle)" "$R"/var/log/cyberdeckzero/update-*.log && grep -q "removed /usr/share/APPLaunch/share/font/AlibabaPuHuiTi-3-35-Thin.ttf (no longer in the bundle)" "$R"/var/log/cyberdeckzero/update-*.log'
check "rollback: the retired fonts are back, byte for byte, same mode; NotoSansCJK untouched" '[ "$(cksum < "$AR/share/font/NotoSerifCJK-Regular.ttc")" = "$SUM_SERIF" ] && [ "$(cksum < "$AR/share/font/AlibabaPuHuiTi-3-35-Thin.ttf")" = "$SUM_THIN" ] && [ "$(stat -c %a "$AR/share/font/NotoSerifCJK-Regular.ttc")" = "$MODE_SERIF" ] && [ "$(cat "$AR/share/font/NotoSansCJK-Regular.ttc")" = "sans font of 0.8.12" ]'
check "rollback: share/ as a whole is as before (replaced files restored, the broken release's own file gone)" '[ "$(cd "$AR/share" && find . -type f | LC_ALL=C sort | while IFS= read -r f; do echo "$f $(cksum < "$f")"; done)" = "$SNAP_SHARE" ] && [ ! -e "$AR/share/images/broken_only.png" ] && [ "$(cat "$AR/share/images/setting_100.png")" = "png 0.8.12" ]'

# an old-style backup (bin, lib and doc only: a helper before this one) still restores what it holds; share/ is left alone
sleep 1
download "$NEWTGZ"
FAKE_KILL_HELPER=1; export FAKE_KILL_HELPER
apply "$NEWTGZ" 0.8.13
unset FAKE_KILL_HELPER
check "an update interrupted after install.sh: the new image set is installed, the retired images gone" '[ "$(st state)" = running ] && [ "$(bin_version)" = 0.8.13 ] && [ ! -e "$AR/share/images/store_100.png" ] && [ ! -e "$AR/share/font/NotoSerifCJK-Regular.ttc" ] && [ ! -e "$AR/share/font/AlibabaPuHuiTi-3-35-Thin.ttf" ] && [ "$(cat "$AR/share/images/setting_100.png")" = "png 0.8.13" ]'
# the backup as an earlier helper made it: only bin, lib and doc
grep -v -e '^dir /usr/share/APPLaunch/share$' -e '^dir /usr/share/APPLaunch/strokes$' -e '^dir /usr/share/APPLaunch/adb$' \
     -e 'preinstalled-desktop-apps.tsv' -e 'vim.desktop.temple' "$B/manifest" > "$B/manifest.old" && mv "$B/manifest.old" "$B/manifest"
rm -rf "$B/files/usr/share/APPLaunch/share"
sed -i -e 's/^boot=.*/boot=an-earlier-boot/' -e 's/^to=.*/to=0.8.99/' "$R/var/lib/cyberdeckzero-update/status"
HT=2
helper resume --user "$U"
HT=4
check "old-style backup: rolled back, bin and lib and doc restored (0.8.12), share/ not touched, no error" '[ $RC = 1 ] && [ "$(st state)" = rolled-back ] && [ "$(bin_version)" = 0.8.12 ] && [ "$(cat "$AR/share/images/setting_100.png")" = "png 0.8.13" ] && [ -f "$R/usr/share/doc/cyberdeckzero-launcher/release-notes-0.8.12.md" ]'

# the free-space check counts the backup: a folder of images too big for the room left is refused, nothing changed
R=$(root pi3a-luckfox35)
first_install --no-boot-config --no-default-source
dd if=/dev/zero of="$R/usr/share/APPLaunch/share/images/big.bin" bs=1024 count=3072 2>/dev/null
download "$NEWTGZ"
SNAP=$(snapshot)
NEED_WORK=$(( $(stat -c %s "$NEWTGZ") / 1024 * 3 + 65536 ))
FAKE_DF_KIB=$(( NEED_WORK + 1000 )); export FAKE_DF_KIB     # the bundle alone would fit (the old rule), the 3 MB backup beside it not
apply "$NEWTGZ" 0.8.13
unset FAKE_DF_KIB
check "free space: the work folder fits but the backup does not: refused (exit 2), nothing changed" '[ $RC = 2 ] && [ "$(st state)" = refused ] && case "$(st reason)" in "not enough space: need "*) true ;; *) false ;; esac && [ "$(snapshot)" = "$SNAP" ] && [ ! -e "$R/var/cache/cyberdeckzero-update" ]'
sleep 1
FAKE_DF_KIB=$(( NEED_WORK + 20000 )); export FAKE_DF_KIB
apply "$NEWTGZ" 0.8.13
unset FAKE_DF_KIB
check "free space: room for both: the update goes ahead (done)" '[ $RC = 0 ] && [ "$(st state)" = done ] && [ "$(bin_version)" = 0.8.13 ]'
# the backup of the last update goes before the next one is made (one backup on the card)
check "backup: one only (no .new left behind), it has the share folder" '[ -d "$R/var/lib/cyberdeckzero-update/backup/files/usr/share/APPLaunch/share" ] && [ ! -e "$R/var/lib/cyberdeckzero-update/backup.new" ]'

# ------------------------------------------------------------------------------------------------------------------
# apt-refresh (report 047 P3): `apt-get update` and nothing else, in the same transient-unit way. A stand-in apt-get that
# writes Status-Fd lines on fd 3 (a slow one: the status is read while it runs) or fails like a network error.
APTBIN="$TMP/aptbin"; mkdir -p "$APTBIN"
cat > "$APTBIN/apt-get" <<'EOF'
#!/bin/sh
printf 'apt-get %s\n' "$*" >> "$FAKE_CALLS"
printf 'env LC_ALL=%s DEBIAN_FRONTEND=%s uid=%s\n' "${LC_ALL:-}" "${DEBIAN_FRONTEND:-}" "$(id -u)" >> "$FAKE_CALLS"
case "${FAKE_APT_MODE:-ok}" in
    ok)
        printf 'dlstatus:1:0.0000:Working\n' >&3; sleep 2
        printf 'dlstatus:2:50.5000:Retrieving file 2 of 4\n' >&3; sleep 3
        printf 'dlstatus:4:100.0000:Retrieving file 4 of 4\n' >&3
        echo "Get:1 http://deb.example/ trixie InRelease"; exit 0 ;;
    fail)
        echo "Err:1 http://deb.example/ trixie InRelease"
        echo "E: Failed to fetch http://deb.example/dists/trixie/InRelease  Temporary failure resolving 'deb.example'"
        exit 100 ;;
esac
EOF
chmod 755 "$APTBIN/apt-get"
aptrefresh() { # aptrefresh ARGS... (foreground; KEEP_CALLS=1: the calls log is not emptied first)
    [ "${KEEP_CALLS:-0}" = 1 ] || : > "$CALLS"
    OUT=$(PATH="$APTBIN:$FAKEBIN:$PATH" UPDATE_ROOT="$R" UPDATE_FAKE_SYSTEM=1 \
          sh "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" "$@" 2>&1); RC=$?
}
ast() { sed -n "s/^$1=//p" "$R/var/lib/cyberdeckzero-update/apt-status" 2>/dev/null | tail -n 1; }
rm -f "$R/var/lib/cyberdeckzero-update/apt-status"
STATUS_BEFORE=$(cat "$R/var/lib/cyberdeckzero-update/status" 2>/dev/null | cksum)
: > "$CALLS"
PATH="$APTBIN:$FAKEBIN:$PATH" UPDATE_ROOT="$R" UPDATE_FAKE_SYSTEM=1 \
    sh "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" apt-refresh --user "$U" > "$TMP/apt.out" 2>&1 &
HP=$!
SEEN=""; SEEN_TEXT=""; i=0
while [ $i -lt 12 ] && kill -0 "$HP" 2>/dev/null; do
    if [ "$(ast percent)" = 50 ]; then SEEN=50; SEEN_TEXT=$(ast text); [ "$(ast state)" = running ] && SEEN_STATE=running; break; fi
    sleep 1; i=$((i + 1))
done
# a second refresh while the first runs is refused (its lock), the first goes on
KEEP_CALLS=1; aptrefresh apt-refresh --user "$U"; KEEP_CALLS=0
SECOND_RC=$RC; SECOND_OUT=$OUT
wait "$HP"; RC=$?; OUT=$(cat "$TMP/apt.out")
check "apt-refresh: apt-get update ran, the status followed its progress (50 %, the text) and ended done at 100 %" '[ $RC = 0 ] && [ "$SEEN" = 50 ] && [ "$SEEN_TEXT" = "Retrieving file 2 of 4" ] && [ "${SEEN_STATE:-}" = running ] && [ "$(ast state)" = done ] && [ "$(ast percent)" = 100 ] && [ -z "$(ast reason)" ] && [ -n "$(ast pid)" ] && [ -n "$(ast boot)" ] && [ -n "$(ast started)" ]'
check "apt-refresh: a second refresh while one runs is refused (exit 2) and says why" '[ "$SECOND_RC" = 2 ] && case "$SECOND_OUT" in *"refresh is running"*) true ;; *) false ;; esac'
check "apt-refresh: only ever apt-get update with Status-Fd, in the C locale, noninteractive, as root (no upgrade, install, remove, dist-upgrade)" '[ "$(grep "^apt-get " "$CALLS" | head -n 1)" = "apt-get update -o APT::Status-Fd=3" ] && ! grep -E "^apt-get .*(upgrade|install|remove|purge|autoremove|full-upgrade|dist-upgrade)" "$CALLS" >/dev/null && grep -q "^env LC_ALL=C DEBIAN_FRONTEND=noninteractive" "$CALLS"'
check "apt-refresh: the status file is world-readable, apt's output is in its log, the update status is untouched" '[ "$(stat -c %a "$R/var/lib/cyberdeckzero-update/apt-status")" = 644 ] && [ -s "$R/var/log/cyberdeckzero/apt-refresh.log" ] && grep -q "^Get:1" "$R/var/log/cyberdeckzero/apt-refresh.log" && [ "$(cat "$R/var/lib/cyberdeckzero-update/status" 2>/dev/null | cksum)" = "$STATUS_BEFORE" ]'
check "apt-refresh: no file in /tmp, nothing left in the run folder but its own files" '[ ! -e "$R/tmp" ] && [ ! -e "$R/run/cyberdeckzero-update/apt.fifo" ]'

FAKE_APT_MODE=fail; export FAKE_APT_MODE
aptrefresh apt-refresh --user "$U"
unset FAKE_APT_MODE
check "apt-refresh: apt fails (a network error): exit 1, state failed with apt's last E: line as the reason" '[ $RC = 1 ] && [ "$(ast state)" = failed ] && case "$(ast reason)" in "E: Failed to fetch http://deb.example/dists/trixie/InRelease"*) true ;; *) false ;; esac'

aptrefresh apt-refresh
check "apt-refresh: no --user: usage error (exit 2), apt-get not run" '[ $RC = 2 ] && ! grep -q "^apt-get " "$CALLS"'
aptrefresh apt-refresh --user "bad name"
check "apt-refresh: a user name that is not one: refused (exit 2), apt-get not run" '[ $RC = 2 ] && ! grep -q "^apt-get " "$CALLS"'
aptrefresh apt-refresh --user "$U" --upgrade
check "apt-refresh: an unknown option: refused (exit 2), apt-get not run" '[ $RC = 2 ] && ! grep -q "^apt-get " "$CALLS"'
OUT=$(PATH="$APTBIN:$FAKEBIN:$PATH" UPDATE_ROOT="$R" sh "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" apt-refresh --user "$U" 2>&1); RC=$?
check "apt-refresh: UPDATE_ROOT without UPDATE_FAKE_SYSTEM is refused (a test hook only)" '[ $RC = 2 ]'
OUT=$(sh "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" help 2>&1); RC=$?
check "help: names apt-refresh" '[ $RC = 0 ] && has "apt-refresh --user NAME"'
rm -f "$R/var/lib/cyberdeckzero-update/apt-status.tmp."*
# left in place for the uninstall below: apt-status goes with /var/lib/cyberdeckzero-update

# ------------------------------------------------------------------------------------------------------------------
# uninstall.sh takes away what P2 adds (the helper, install.conf, its status, backup, logs, work and run folders)
: > "$CALLS"
mkdir -p "$R/var/cache/cyberdeckzero-update" "$R$UHOME/.cache/cyberdeckzero/update" "$R$UHOME/.local/state/cyberdeckzero"
OUT=$(PATH="$FAKEBIN:$PATH" HOME="$R$UHOME" UNINSTALL_ROOT="$R" APPLAUNCH_SYSFS="$TMP/no-sys" \
      sh "$TMP/new/bundle/uninstall.sh" --dry-run --no-boot-config 2>&1); RC=$?
check "uninstall dry-run: the update helper's files planned, nothing removed" '[ $RC = 0 ] && has "cyberdeckzero-update, lib/libapplaunch_vfb.so" && has "/var/lib/cyberdeckzero-update (its status and last backup)" && has "install options $R/etc/applaunch/install.conf" && [ -d "$R/var/lib/cyberdeckzero-update/backup" ]'
OUT=$(PATH="$FAKEBIN:$PATH" HOME="$R$UHOME" UNINSTALL_ROOT="$R" APPLAUNCH_SYSFS="$TMP/no-sys" \
      sh "$TMP/new/bundle/uninstall.sh" --yes --no-boot-config 2>&1); RC=$?
check "uninstall: the helper, install.conf, the status, the backup, the logs, the work and run folders gone" '[ $RC = 0 ] && [ ! -e "$R/usr/share/APPLaunch/bin/cyberdeckzero-update" ] && [ ! -e "$R/etc/applaunch" ] && [ ! -e "$R/var/lib/cyberdeckzero-update" ] && [ ! -e "$R/var/log/cyberdeckzero" ] && [ ! -e "$R/var/cache/cyberdeckzero-update" ] && [ ! -e "$R/run/cyberdeckzero-update" ]'
check "uninstall without --purge-config: the user's update state and cache kept" '[ -d "$R$UHOME/.cache/cyberdeckzero/update" ] && [ -d "$R$UHOME/.local/state/cyberdeckzero" ]'
OUT=$(PATH="$FAKEBIN:$PATH" HOME="$R$UHOME" UNINSTALL_ROOT="$R" APPLAUNCH_SYSFS="$TMP/no-sys" \
      sh "$TMP/new/bundle/uninstall.sh" --yes --no-boot-config --purge-config 2>&1); RC=$?
check "uninstall --purge-config: ~/.cache/cyberdeckzero and ~/.local/state/cyberdeckzero gone" '[ $RC = 0 ] && [ ! -e "$R$UHOME/.cache/cyberdeckzero" ] && [ ! -e "$R$UHOME/.local/state/cyberdeckzero" ]'

check "nothing written outside the temp dir" '[ ! -e /var/lib/cyberdeckzero-update ] && [ ! -e /var/cache/cyberdeckzero-update ] && [ ! -e /run/cyberdeckzero-update ] && [ ! -e /etc/applaunch/install.conf ]'

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
