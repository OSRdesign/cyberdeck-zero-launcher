#!/bin/sh
# PC test of the network share helper (cyberdeckzero-share): every verb, every allow-list rejection, the calling-user
# and credentials-file checks, the add failure words, remove (idempotent), foreign units, and "stdin is never read, no
# password leaks". Fake root (CDZ_SHARE_SYSROOT, honoured only when not root) and stand-ins for mount, umount,
# mountpoint, systemctl, systemd-escape, id and mount.cifs first on PATH. Changes nothing outside a temp dir, needs no
# root and no board.
#   sh projects/APPLaunch/pizero2w/tests/test-share-helper.sh
set -u
if [ "$(id -u)" = 0 ]; then
    echo "run it as a normal user: as root a mistake could reach the live system" >&2
    exit 2
fi
HERE=$(cd "$(dirname "$0")" && pwd)
H="$HERE/../cyberdeckzero-share"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); printf 'ok   %s\n' "$(printf '%s' "$1" | tr '\n\r\t' '   ')"; }
ko() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$(printf '%s' "$1" | tr '\n\r\t' '   ')"; [ -z "${2:-}" ] || printf '%s\n' "$2" | sed 's/^/     | /'; }
has() { case "$OUT" in *"$1"*) return 0 ;; esac; return 1; }
check() { if eval "$2"; then ok "$1"; else ko "$1" "OUT=$OUT RC=$RC
$(cat "$TMP/stderr" 2>/dev/null)
$(tail -n 8 "$LOG" 2>/dev/null)"; fi; }

UIDN=$(id -u)
GIDN=$(id -g)
UN=$(id -un)
R="$TMP/root"
ST="$TMP/state"
LOG="$TMP/calls.log"
BIN="$TMP/bin"
export FAKE_LOG="$LOG" FAKE_STATE="$ST" REAL_UID="$UIDN"
mkdir -p "$BIN"

cat > "$BIN/mount" <<'EOF'
#!/bin/sh
# stand-in mount: args -t cifs WHAT PATH -o OPTS. FAKE_MOUNT=ok|err|hang. Records its own stdin (must be empty).
echo "mount $*" >> "$FAKE_LOG"
cat > "$FAKE_STATE/mount.stdin"
# the credentials file must be readable while the test mount runs (only its existence is logged, never a value)
case "$6" in *credentials=*) f=${6#*credentials=}; f=${f%%,*}; if grep -q '^password=' "$f" 2>/dev/null; then echo "credfile-readable" >> "$FAKE_LOG"; fi ;; esac
case ${FAKE_MOUNT:-ok} in
    ok) echo "$4" >> "$FAKE_STATE/mounted"; exit 0 ;;
    hang) exec sleep 30 ;;
    err) cat "$FAKE_MOUNT_ERR" >&2; exit "${FAKE_MOUNT_RC:-32}" ;;
esac
EOF
cat > "$BIN/umount" <<'EOF'
#!/bin/sh
echo "umount $*" >> "$FAKE_LOG"
lazy=0; [ "$1" = -l ] && { lazy=1; shift; }
if [ "${FAKE_UMOUNT_BUSY:-0}" = 1 ] && [ "$lazy" = 0 ]; then echo "umount: $1: target is busy" >&2; exit 16; fi
grep -vxF -- "$1" "$FAKE_STATE/mounted" > "$FAKE_STATE/mounted.new"; mv "$FAKE_STATE/mounted.new" "$FAKE_STATE/mounted"
exit 0
EOF
cat > "$BIN/mountpoint" <<'EOF'
#!/bin/sh
grep -qxF -- "$2" "$FAKE_STATE/mounted"
EOF
cat > "$BIN/systemctl" <<'EOF'
#!/bin/sh
echo "systemctl $*" >> "$FAKE_LOG"
[ "${FAKE_SYSTEMCTL_FAIL:-}" = "$1" ] && exit 1
case "$1" in
    is-active) grep -qxF -- "$3" "$FAKE_STATE/active" ;;
    is-failed) grep -qxF -- "$3" "$FAKE_STATE/failed" ;;
    *) exit 0 ;;
esac
EOF
cat > "$BIN/systemd-escape" <<'EOF'
#!/bin/sh
# stand-in: --path /a/b-c -> a-b\x2dc
p=${2#/}
printf '%s\n' "$p" | sed 's/-/\\x2d/g; s|/|-|g'
EOF
cat > "$BIN/id" <<'EOF'
#!/bin/sh
if [ $# -eq 2 ] && [ "$1" = -u ]; then echo "${FAKE_ID_UID:-$REAL_UID}"; exit 0; fi
exec /usr/bin/id "$@"
EOF
printf '#!/bin/sh\nexit 0\n' > "$BIN/mount.cifs"
chmod 755 "$BIN"/*

reset() {
    rm -rf "$R" "$ST"
    mkdir -p "$R/etc/systemd/system" "$R/run" "$R/mnt" "$ST"
    : > "$LOG"; : > "$ST/mounted"; : > "$ST/active"; : > "$ST/failed"
    unset FAKE_MOUNT FAKE_MOUNT_ERR FAKE_MOUNT_RC FAKE_UMOUNT_BUSY FAKE_SYSTEMCTL_FAIL FAKE_ID_UID
    RUNENV=""; RUNUNSET=""; STDIN_DATA="nothing"
}
# run the helper: output $OUT, status $RC, stderr in $TMP/stderr. RUNENV "A=b C=d" (no spaces in values), RUNUNSET one variable name
run() {
    SUD=""
    for kv in "SUDO_UID=$UIDN" "SUDO_GID=$GIDN" "SUDO_USER=$UN"; do [ "${kv%%=*}" = "$RUNUNSET" ] || SUD="$SUD $kv"; done
    OUT=$(printf '%s\n' "$STDIN_DATA" | env CDZ_SHARE_SYSROOT="$R" CDZ_SHARE_PATH="$BIN" \
        CDZ_SHARE_MOUNT_TIMEOUT="${CDZ_SHARE_MOUNT_TIMEOUT:-10}" $SUD $RUNENV \
        sh "$H" "$@" 2> "$TMP/stderr"); RC=$?
}
SYSD="$R/etc/systemd/system"
MU="mnt-cyberdeck-nas.mount"; AU="mnt-cyberdeck-nas.automount"
nothing_left() {
    [ -z "$(ls -A "$SYSD")" ] && [ -z "$(ls -A "$R/mnt/cyberdeck" 2>/dev/null)" ] && [ ! -e "$R/etc/cyberdeckzero" ] \
        && [ -z "$(ls -d "$R"/run/cyberdeckzero-share/add.* 2>/dev/null)" ] && [ ! -s "$ST/mounted" ]
}
no_mount_call() { ! grep -q '^mount ' "$LOG"; }
mkcred() { # NAME CONTENT [MODE] -> CRED (the path as the helper sees it)
    d="$R/run/user/$UIDN/cyberdeckzero-share"; mkdir -p "$d"
    printf '%s' "$2" > "$d/$1.cred"; chmod "${3:-600}" "$d/$1.cred"
    CRED="/run/user/$UIDN/cyberdeckzero-share/$1.cred"
}
PWD1='s3cr3t,P@ss word!'
GOODCRED="username=bob
password=$PWD1
"
add_guest() { run add --name nas --host 192.168.1.5 --share music "$@"; }

# ---------------------------------------------------------------------------------------------------------------
# static facts about the file
check "the helper is valid sh (sh -n)" 'sh -n "$H"'
if command -v dash > /dev/null 2>&1; then check "the helper is valid dash" 'dash -n "$H"'; fi
check "the first command closes stdin (exec < /dev/null): the sudo password may arrive there" '[ "$(grep -v "^#" "$H" | grep -v "^$" | head -n 1)" = "exec < /dev/null" ]'
check "no eval and no read of stdin in the helper" '! grep -v "^ *#" "$H" | grep -Eq "(^|[^a-z_])eval |(^|[^a-z_])read |/dev/stdin|<&0"'
check "no caller text is put into -o: the options are built from SUDO_UID/SUDO_GID, the fixed list and a path the helper makes" 'grep -q "^OPTS_BASE=\"ro,nosuid,nodev,noexec,uid=\$SUDO_UID,gid=\$SUDO_GID," "$H" && ! grep -n "OPTS" "$H" | grep -E "HOST|SHARE|SUBF|CUSER|PW" | grep -q .'

# ---------------------------------------------------------------------------------------------------------------
reset
run version
check "version: ok 1" '[ $RC = 0 ] && [ "$OUT" = "ok 1" ]'
run
check "no verb: err usage, status 2" '[ $RC = 2 ] && has "err usage"'
run frobnicate
check "unknown verb: err usage" '[ $RC = 2 ] && has "err usage"'
run version extra
check "version with an argument: err usage" '[ $RC = 2 ] && has "err usage"'
run list --all
check "list with an argument: err usage" '[ $RC = 2 ] && has "err usage"'
run remove
check "remove without --name: err usage" '[ $RC = 2 ] && has "err usage"'
CDZ_SHARE_SYSROOT="" env -u CDZ_SHARE_SYSROOT SUDO_UID="$UIDN" SUDO_GID="$GIDN" SUDO_USER="$UN" sh "$H" add --name nas --host 1.2.3.4 --share s > "$TMP/o" 2>&1 < /dev/null; RC=$?; OUT=$(cat "$TMP/o")
check "no test hook and not root: err other (add and remove need root), nothing done" '[ $RC = 1 ] && has "err other" && has "root"'

# ---------------------------------------------------------------------------------------------------------------
# add, guest
reset
add_guest
check "add guest: ok path=/mnt/cyberdeck/nas, status 0" '[ $RC = 0 ] && [ "$OUT" = "ok path=/mnt/cyberdeck/nas" ]'
check "add guest: both units written 0644, the mount point made" '[ "$(stat -c %a "$SYSD/$MU")" = 644 ] && [ "$(stat -c %a "$SYSD/$AU")" = 644 ] && [ -d "$R/mnt/cyberdeck/nas" ]'
check "add guest: marker line first, no temp files left" 'head -n 1 "$SYSD/$MU" | grep -q "^# cyberdeckzero-share v1: made by the launcher helper" && head -n 1 "$SYSD/$AU" | grep -q "^# cyberdeckzero-share v1: made" && [ "$(ls "$SYSD" | wc -l)" = 2 ]'
check "add guest: mount unit has What, Where, Type, TimeoutSec" 'grep -qx "What=//192.168.1.5/music" "$SYSD/$MU" && grep -qx "Where=/mnt/cyberdeck/nas" "$SYSD/$MU" && grep -qx "Type=cifs" "$SYSD/$MU" && grep -qx "TimeoutSec=10" "$SYSD/$MU"'
check "add guest: fixed options (ro, nosuid,nodev,noexec, uid/gid from sudo, soft, echo_interval, guest), no vers=, no SMB1" 'grep -qx "Options=ro,nosuid,nodev,noexec,uid=$UIDN,gid=$GIDN,forceuid,forcegid,file_mode=0444,dir_mode=0555,soft,echo_interval=10,guest" "$SYSD/$MU" && ! grep -q "vers=" "$SYSD/$MU"'
check "add guest: automount has Where, TimeoutIdleSec=60 and WantedBy" 'grep -qx "Where=/mnt/cyberdeck/nas" "$SYSD/$AU" && grep -qx "TimeoutIdleSec=60" "$SYSD/$AU" && grep -qx "WantedBy=remote-fs.target" "$SYSD/$AU"'
check "add guest: the test mount used the same options and was unmounted, nothing left in the work folder" 'grep -q "^mount -t cifs //192.168.1.5/music .*/mnt -o ro,nosuid,nodev,noexec,uid=$UIDN,gid=$GIDN,forceuid,forcegid,file_mode=0444,dir_mode=0555,soft,echo_interval=10,guest$" "$LOG" && [ ! -s "$ST/mounted" ] && [ -z "$(ls -d "$R"/run/cyberdeckzero-share/add.* 2>/dev/null)" ]'
check "add guest: daemon-reload, then enable --now of the automount only (never the mount)" 'grep -n "" "$LOG" | grep -q "systemctl daemon-reload" && grep -qF "systemctl enable --now $AU" "$LOG" && ! grep -qF "enable --now $MU" "$LOG" && [ "$(grep -n "daemon-reload" "$LOG" | head -n 1 | cut -d: -f1)" -lt "$(grep -n "enable --now" "$LOG" | head -n 1 | cut -d: -f1)" ]'
check "add guest: the test mount's stdin was /dev/null (empty)" '[ ! -s "$ST/mount.stdin" ]'
check "add guest: no credentials folder made" '[ ! -e "$R/etc/cyberdeckzero" ]'

# subfolder, a name with a dash (systemd-escape), a hostname
reset
run add --name my-share --host nas.local --share Media --subfolder music/rock-1
check "add: a dash in the name and a subfolder: ok, unit named by systemd-escape, What with the subfolder" '[ $RC = 0 ] && [ "$OUT" = "ok path=/mnt/cyberdeck/my-share" ] && [ -f "$SYSD/mnt-cyberdeck-my\\x2dshare.mount" ] && grep -qx "What=//nas.local/Media/music/rock-1" "$SYSD/mnt-cyberdeck-my\\x2dshare.mount" && grep -qx "Where=/mnt/cyberdeck/my-share" "$SYSD/mnt-cyberdeck-my\\x2dshare.automount"'
run list
check "list: one line with the subfolder, then ok" '[ $RC = 0 ] && [ "$OUT" = "my-share nas.local Media music/rock-1 inactive
ok" ]'
run remove --name my-share
check "remove: the dashed name's units and mount point gone" '[ $RC = 0 ] && [ "$OUT" = ok ] && nothing_left'

# ---------------------------------------------------------------------------------------------------------------
# add with a user: the credentials file
reset
mkcred abcdef12 "$GOODCRED"
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
CF="$R/etc/cyberdeckzero/shares/nas.cred"
check "add with user: ok, credentials copied root-only (0600, folder 0700), the caller's file deleted" '[ $RC = 0 ] && [ "$OUT" = "ok path=/mnt/cyberdeck/nas" ] && [ "$(stat -c %a "$CF")" = 600 ] && [ "$(stat -c %a "$R/etc/cyberdeckzero/shares")" = 700 ] && [ ! -e "$R$CRED" ]'
check "add with user: the stored file holds username= and password= (the password unchanged, comma and space kept)" '[ "$(cat "$CF")" = "username=bob
password=$PWD1" ]'
check "add with user: the unit points at the stored file and holds no secret" 'grep -qx "Options=ro,nosuid,nodev,noexec,uid=$UIDN,gid=$GIDN,forceuid,forcegid,file_mode=0444,dir_mode=0555,soft,echo_interval=10,credentials=/etc/cyberdeckzero/shares/nas.cred" "$SYSD/$MU" && ! grep -rqF "s3cr3t" "$SYSD" && ! grep -q "guest" "$SYSD/$MU"'
check "add with user: the test mount read the credentials file; the password is in no file but the stored one and not in the output, log or stderr" 'grep -q credfile-readable "$LOG" && [ "$(grep -rlF s3cr3t "$TMP" | sort)" = "$CF" ] && ! has s3cr3t && ! grep -q s3cr3t "$TMP/stderr"'
run remove --name nas
check "remove: the stored credentials and the folder of the units go too" '[ $RC = 0 ] && [ ! -e "$CF" ] && [ ! -e "$SYSD/$MU" ] && [ ! -d "$R/mnt/cyberdeck/nas" ]'
# domain key
reset
mkcred abcdef12 "username=bob
password=pw
domain=WORK-GROUP.lan
"
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "credentials with a domain line: stored as username, password, domain" '[ $RC = 0 ] && [ "$(cat "$R/etc/cyberdeckzero/shares/nas.cred")" = "username=bob
password=pw
domain=WORK-GROUP.lan" ]'

# ---------------------------------------------------------------------------------------------------------------
# allow-list rejections: every one is err usage (status 2), nothing is mounted, nothing is left
NL='
'
rej() { # LABEL args...
    lab=$1; shift
    reset
    run "$@"
    check "rejected: $lab" '[ $RC = 2 ] && has "err usage" && no_mount_call && nothing_left'
}
GOOD="--host 192.168.1.5 --share music"
for v in 'a,b' 'a b' '-x' '../x' 'a/b' 'a.b' 'x$(id)' 'a`b' "a${NL}b" 'a;b' 'né' 'a%b' "a$(printf '\t')b" "a$(printf '\r')" 'a=b' 'a*' '.' '..' 'a:b'; do
    rej "name [$v]" add --name "$v" $GOOD
done
rej "name empty" add --name "" $GOOD
rej "name of 33 characters" add --name abcdefghijklmnopqrstuvwxyz0123456 $GOOD
for v in 'a b' '-h' '1.2.3.999' '1.2.3' '010.1.1.1' '1.2.3.4.5' 'h,ost' 'h/ost' 'host..x' '.local' 'host-' '-host' 'a_b.local' '::1' 'fe80::1' 'h;x' "h${NL}x" 'h$x' 'hé.lan' 'a..b' '1.2.3.4/24' 'user@host' '[::1]' 'h:445' 'host.'; do
    rej "host [$v]" add --name nas --host "$v" --share music
done
rej "host of 254 characters" add --name nas --host "$(printf 'a%.0s' $(seq 1 63)).$(printf 'a%.0s' $(seq 1 63)).$(printf 'a%.0s' $(seq 1 63)).$(printf 'a%.0s' $(seq 1 62))x" --share music
for v in 'a b' 'a/b' 'a\b' 'a,b' '..' '.' 'a..b' '' 'é' 'a%b' 'a(b)' 'a;b' "a${NL}b" "a$(printf '\001')b" 'a"b' "a'b" 'a=b'; do
    rej "share [$v]" add --name nas --host 192.168.1.5 --share "$v"
done
rej "share of 81 characters" add --name nas --host 192.168.1.5 --share "$(printf 'a%.0s' $(seq 1 81))"
for v in '../x' '/abs' 'a/../b' 'a,b' 'a//b' 'a/' './a' 'a/.' 'a b' 'a\b' '..' '.' "a${NL}b" 'a;b' 'é' '' 'a%b'; do
    rej "subfolder [$v]" add --name nas $GOOD --subfolder "$v"
done
for v in 'a,b' 'a b' '-u' 'u=x' 'u/x' "u${NL}x" 'é' 'u;x' ''; do
    rej "user [$v]" add --name nas $GOOD --user "$v" --credfile /run/user/$UIDN/cyberdeckzero-share/abcdef12.cred
done
rej "option -o" add --name nas $GOOD -o rw,suid
rej "option --options" add --name nas $GOOD --options rw
rej "option -o as a value of --share" add --name nas --host 192.168.1.5 --share "x -o suid"
rej "option injection through the host (comma)" add --name nas --host "1.2.3.4,uid=0" --share music
rej "option injection through the user (comma)" add --name nas $GOOD --user "bob,sec=none" --credfile /run/user/$UIDN/cyberdeckzero-share/abcdef12.cred
rej "a password flag" add --name nas $GOOD --password hunter2
rej "--password-stdin" add --name nas $GOOD --user bob --password-stdin
rej "--name twice" add --name nas --name other $GOOD
rej "--host twice" add --name nas --host 1.2.3.4 --host 1.2.3.5 --share music
rej "a flag without its value" add --name nas $GOOD --subfolder
rej "no --host" add --name nas --share music
rej "no --share" add --name nas --host 1.2.3.4
rej "a positional argument" add nas --host 1.2.3.4 --share music
rej "--user without --credfile" add --name nas $GOOD --user bob
rej "--credfile without --user" add --name nas $GOOD --credfile /run/user/$UIDN/cyberdeckzero-share/abcdef12.cred
rej "remove with extra flags" remove --name nas --host 1.2.3.4
rej "remove with a bad name" remove --name "../etc"
rej "remove with a name starting with a dash" remove --name -rf
# the value of a flag that looks like another flag
rej "--name followed by a flag" add --name --host 1.2.3.4 --share music

# ---------------------------------------------------------------------------------------------------------------
# the calling user
sudo_case() { # LABEL ENV-ASSIGNMENT|-u VAR
    reset
    case $2 in -u) RUNUNSET=$3 ;; *) RUNENV=$2 ;; esac
    add_guest
    check "caller: $1: err usage, nothing mounted, nothing left" '[ $RC = 2 ] && has "err usage" && no_mount_call && nothing_left'
}
sudo_case "SUDO_UID missing" -u SUDO_UID
sudo_case "SUDO_UID is root" SUDO_UID=0
sudo_case "SUDO_UID 0000" SUDO_UID=0000
sudo_case "SUDO_UID not a number" SUDO_UID=abc
sudo_case "SUDO_UID with a sign" SUDO_UID=-1
sudo_case "SUDO_UID too long" SUDO_UID=12345678901
sudo_case "SUDO_GID missing" -u SUDO_GID
sudo_case "SUDO_GID not a number" SUDO_GID=x
sudo_case "SUDO_USER missing" -u SUDO_USER
sudo_case "SUDO_USER is root" SUDO_USER=root
sudo_case "SUDO_USER odd characters" 'SUDO_USER=a;b'
sudo_case "SUDO_USER and SUDO_UID disagree (id says another uid)" FAKE_ID_UID=4242
reset; RUNENV="SUDO_UID=$UIDN"; add_guest
check "caller: the right ids pass" '[ $RC = 0 ]'
# list and version need none of it
reset; RUNUNSET=SUDO_UID; run list
check "list works without SUDO_UID (no root needed to look)" '[ $RC = 0 ] && [ "$OUT" = ok ]'

# ---------------------------------------------------------------------------------------------------------------
# credentials file checks
credcase() { # LABEL (the case is prepared by the caller: CRED, extra env)
    run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
    check "credfile: $1: err usage, nothing mounted, nothing left" '[ $RC = 2 ] && has "err usage" && no_mount_call && nothing_left'
}
reset; mkcred abcdef12 "$GOODCRED" 640; credcase "mode 0640"
check "credfile: a refused file (mode) is not deleted" '[ -f "$R$CRED" ]'
reset; mkcred abcdef12 "$GOODCRED" 644; credcase "mode 0644"
reset; mkcred abcdef12 "$GOODCRED" 400; credcase "mode 0400"
reset; mkcred abcdef12 "$GOODCRED" 700; credcase "mode 0700"
# a file of another owner: the helper acts for another uid (the id stand-in agrees), the file is ours
reset; O=54321; d="$R/run/user/$O/cyberdeckzero-share"; mkdir -p "$d"; printf '%s' "$GOODCRED" > "$d/abcdef12.cred"; chmod 600 "$d/abcdef12.cred"
RUNENV="SUDO_UID=$O FAKE_ID_UID=$O"
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "/run/user/$O/cyberdeckzero-share/abcdef12.cred"
check "credfile: wrong owner: err usage, file kept, nothing left" '[ $RC = 2 ] && has "owned" && no_mount_call && nothing_left && [ -f "$d/abcdef12.cred" ]'
reset; mkcred realfile1 "$GOODCRED"; ln -s "$R$CRED" "$R/run/user/$UIDN/cyberdeckzero-share/abcdef12.cred"; CRED="/run/user/$UIDN/cyberdeckzero-share/abcdef12.cred"; credcase "a symlink to a good file"
check "credfile: the symlink and its target are left alone" '[ -L "$R$CRED" ] && [ -f "$R/run/user/$UIDN/cyberdeckzero-share/realfile1.cred" ]'
reset; mkdir -p "$R/run/user/$UIDN/cyberdeckzero-share/abcdef12.cred"; CRED="/run/user/$UIDN/cyberdeckzero-share/abcdef12.cred"; credcase "a directory"
reset; mkdir -p "$R/run/user/$UIDN/cyberdeckzero-share"; CRED="/run/user/$UIDN/cyberdeckzero-share/abcdef12.cred"; credcase "missing file"
reset; mkdir -p "$R/tmp"; printf '%s' "$GOODCRED" > "$R/tmp/abcdef12.cred"; chmod 600 "$R/tmp/abcdef12.cred"; CRED=/tmp/abcdef12.cred; credcase "a path outside /run/user/<uid>/cyberdeckzero-share"
reset; mkcred abcdef12 "$GOODCRED"; CRED="/run/user/$UIDN/cyberdeckzero-share/../abcdef12.cred"; credcase "a .. path"
reset; mkcred abcdef12 "$GOODCRED"; CRED="/run/user/$UIDN/cyberdeckzero-share/sub/abcdef12.cred"; credcase "a nested path"
reset; mkcred abcdef12 "$GOODCRED"; CRED="/run/user/$((UIDN + 1))/cyberdeckzero-share/abcdef12.cred"; credcase "another user's folder"
reset; mkcred abcdef12 "$GOODCRED"; CRED="/run/user/$UIDN/cyberdeckzero-share/abcdef12.txt"; credcase "not .cred"
reset; mkcred abc "$GOODCRED"; credcase "a name shorter than 8"
reset; mkcred 'abcdef12' "$GOODCRED"; CRED="/run/user/$UIDN/cyberdeckzero-share/abc def12.cred"; credcase "a space in the name"
reset; mkcred abcdef12 "$GOODCRED"; CRED="${CRED}$NL"; credcase "a newline in the path"
big=$(printf 'x%.0s' $(seq 1 2000))
reset; mkcred abcdef12 "username=bob
password=$big
"; credcase "too big (2000 bytes)"
check "credfile: a file refused for its content (owner and mode were right) is deleted" '[ ! -e "$R$CRED" ]'
reset; mkcred abcdef12 "username=bob
password=$(printf 'x%.0s' $(seq 1 257))
"; credcase "password of 257 bytes"
reset; mkcred abcdef12 "username=bob
password=pw
foo=bar
"; credcase "an unknown key"
reset; mkcred abcdef12 "username=eve
password=pw
"; credcase "username different from --user"
reset; mkcred abcdef12 "username=bob
"; credcase "no password"
reset; mkcred abcdef12 "password=
"; credcase "an empty password"
reset; mkcred abcdef12 "password=a
password=b
"; credcase "password twice"
reset; mkcred abcdef12 "password=pw
no equals sign
"; credcase "a line without ="
reset; mkcred abcdef12 "password=pw

"; credcase "an empty line"
reset; mkcred abcdef12 "password=pw
domain=bad domain
"; credcase "a domain with a space"
reset; mkcred abcdef12 "password=pw
domain=x;y
"; credcase "a domain with a semicolon"
reset; mkcred abcdef12 ""; credcase "an empty file"
reset; mkcred abcdef12 "password=pw$(printf '\r')
"; credcase "a carriage return"
reset; d="$R/run/user/$UIDN/cyberdeckzero-share"; mkdir -p "$d"; printf 'password=pw\000x\n' > "$d/abcdef12.cred"; chmod 600 "$d/abcdef12.cred"; CRED="/run/user/$UIDN/cyberdeckzero-share/abcdef12.cred"; credcase "a NUL byte"
reset; mkcred abcdef12 "password=pw$(printf '\033')[31m
"; credcase "an escape character"
reset; mkcred abcdef12 "password=pw
"; run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "credfile: a file with only password= is fine (username comes from --user)" '[ $RC = 0 ] && [ "$(cat "$R/etc/cyberdeckzero/shares/nas.cred")" = "username=bob
password=pw" ]'
reset; mkcred abcdef12 "password=päss€wörd
"; run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "credfile: a password with non-ASCII characters is kept as it is" '[ $RC = 0 ] && grep -qx "password=päss€wörd" "$R/etc/cyberdeckzero/shares/nas.cred"'

# ---------------------------------------------------------------------------------------------------------------
# add failures: the word from the canned mount error, nothing left behind
failcase() { # WORD RC STDERR-TEXT [LABEL]
    reset
    W=$1
    FAKE_MOUNT=err; FAKE_MOUNT_RC=$2; FAKE_MOUNT_ERR="$TMP/err.txt"; export FAKE_MOUNT FAKE_MOUNT_RC FAKE_MOUNT_ERR
    printf '%s\n' "$3" > "$TMP/err.txt"
    add_guest
    check "add failure ${4:-$3}: err $1, status 1, nothing left, no enable" '[ $RC = 1 ] && [ "${OUT%% *}" = err ] && [ "$(printf "%s" "$OUT" | cut -d" " -f2)" = "$W" ] && nothing_left && ! grep -q "enable" "$LOG"'
}
failcase nomodule 32 "mount error: cifs filesystem not supported by the system"
failcase nomodule 32 "mount error(19): No such device"
failcase unreachable 32 "mount error: could not resolve address for nas.invalid: Unknown error"
failcase unreachable 32 "mount error: could not connect to 192.168.1.250"
failcase unreachable 32 "mount error(113): No route to host"
failcase unreachable 32 "mount error(112): Host is down"
failcase unreachable 32 "mount error(111): Connection refused"
failcase timeout 32 "mount error(110): Connection timed out"
failcase denied 32 "mount error(13): Permission denied"
failcase denied 32 "CIFS: VFS: cifs_mount failed w/return code = -13 NT_STATUS_LOGON_FAILURE"
failcase denied 32 "CIFS: VFS: Send error in SessSetup = -13 NT_STATUS_ACCESS_DENIED"
failcase notfound 32 "mount error(2): No such file or directory"
failcase notfound 32 "CIFS: VFS: BAD_NETWORK_NAME: \\\\192.168.1.5\\nope NT_STATUS_BAD_NETWORK_NAME"
failcase notfound 32 "mount error(6): No such device or address"
failcase other 32 "mount error(95): Operation not supported"
failcase other 1 "" "(no message, status 1)"
failcase timeout 124 "" "(status 124)"
reset; CDZ_SHARE_MOUNT_TIMEOUT=1; export FAKE_MOUNT=hang
add_guest
check "add failure: a mount that hangs is killed after the timeout: err timeout, nothing left" '[ $RC = 1 ] && has "err timeout" && nothing_left'
unset CDZ_SHARE_MOUNT_TIMEOUT FAKE_MOUNT
# "other" carries the sanitized reason
reset; FAKE_MOUNT=err; FAKE_MOUNT_RC=32; FAKE_MOUNT_ERR="$TMP/err.txt"; export FAKE_MOUNT FAKE_MOUNT_RC FAKE_MOUNT_ERR
printf 'mount error(95): Operation not supported\nRefer to the mount.cifs(8) manual page\n' > "$TMP/err.txt"
add_guest
check "other: the first line of the mount error is the reason" '[ "$OUT" = "err other mount error(95): Operation not supported" ]'
printf '\033[31mboom\033[0m\a%s\n' "$(printf 'y%.0s' $(seq 1 400))" > "$TMP/err.txt"
add_guest
check "other: control characters removed, the reason cut at 160 characters, one line" '[ $RC = 1 ] && [ "$(printf "%s\n" "$OUT" | wc -l)" = 1 ] && [ ${#OUT} -le $((10 + 160)) ] && ! printf "%s" "$OUT" | grep -q "$(printf "\033")" && ! printf "%s" "$OUT" | grep -q "$(printf "\a")"'
# the password and credential text never appear in the reason
reset; mkcred abcdef12 "username=bob
password=Zq9-secret
"
FAKE_MOUNT=err; FAKE_MOUNT_RC=32; FAKE_MOUNT_ERR="$TMP/err.txt"; export FAKE_MOUNT FAKE_MOUNT_RC FAKE_MOUNT_ERR
printf 'mount error: bad option credentials=/x/y username=bob password=Zq9-secret pwd=1 Zq9-secret done\n' > "$TMP/err.txt"
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "failure with a user: the password and credential text are redacted from the reason" '[ $RC = 1 ] && has "err other" && ! has "Zq9-secret" && ! has "username=bob" && ! has "password=Z" && ! has "credentials=/x"'
check "failure with a user: no credentials stored, the caller's temp file deleted, nothing left, no leak anywhere" 'nothing_left && [ ! -e "$R$CRED" ] && ! grep -rqF "Zq9-secret" "$R" "$TMP/stderr" && [ -z "$(grep -rlF Zq9-secret "$TMP" | grep -v err.txt)" ]'
unset FAKE_MOUNT FAKE_MOUNT_RC FAKE_MOUNT_ERR
# no mount.cifs
reset; mv "$BIN/mount.cifs" "$BIN/mount.cifs.off"
add_guest
check "add without mount.cifs: err nohelper, status 1, nothing mounted, nothing left" '[ $RC = 1 ] && has "err nohelper" && no_mount_call && nothing_left'
mv "$BIN/mount.cifs.off" "$BIN/mount.cifs"
# systemctl fails: the whole add is rolled back
reset; mkcred abcdef12 "$GOODCRED"; export FAKE_SYSTEMCTL_FAIL=enable
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "add: the automount cannot be enabled: err other, units, mount point and credentials rolled back" '[ $RC = 1 ] && has "err other" && [ -z "$(ls -A "$SYSD")" ] && [ ! -d "$R/mnt/cyberdeck/nas" ] && [ ! -e "$R/etc/cyberdeckzero/shares/nas.cred" ]'
reset; export FAKE_SYSTEMCTL_FAIL=daemon-reload; add_guest
check "add: daemon-reload fails: err other, rolled back" '[ $RC = 1 ] && has "err other" && [ -z "$(ls -A "$SYSD")" ] && [ ! -d "$R/mnt/cyberdeck/nas" ]'
unset FAKE_SYSTEMCTL_FAIL

# ---------------------------------------------------------------------------------------------------------------
# names that are taken
reset; add_guest
SUM=$(cat "$SYSD/$MU" "$SYSD/$AU" | cksum)
add_guest
check "add the same name again: err exists, the first share untouched" '[ $RC = 1 ] && has "err exists" && [ "$(cat "$SYSD/$MU" "$SYSD/$AU" | cksum)" = "$SUM" ]'
run add --name nas --host 10.0.0.2 --share other
check "add the same name with other data: err exists, nothing replaced" '[ $RC = 1 ] && has "err exists" && grep -qx "What=//192.168.1.5/music" "$SYSD/$MU"'
reset; printf '[Mount]\nWhat=//x/y\nWhere=/mnt/cyberdeck/nas\nType=cifs\n' > "$SYSD/$MU"; SUM=$(cksum < "$SYSD/$MU")
add_guest
check "a foreign unit with that name: err exists, left as it is, no test mount" '[ $RC = 1 ] && has "err exists" && has "not made by" && [ "$(cksum < "$SYSD/$MU")" = "$SUM" ] && no_mount_call && [ ! -e "$SYSD/$AU" ]'
run remove --name nas
check "remove of a foreign unit: refused, left as it is" '[ $RC = 1 ] && has "err other" && has "not made by" && [ "$(cksum < "$SYSD/$MU")" = "$SUM" ]'
reset; printf '[Automount]\nWhere=/mnt/cyberdeck/nas\n' > "$SYSD/$AU"; add_guest
check "a foreign automount with that name: err exists" '[ $RC = 1 ] && has "err exists" && [ ! -e "$SYSD/$MU" ]'
reset; printf 'x\n' > "$TMP/elsewhere"; ln -s "$TMP/elsewhere" "$SYSD/$MU"; add_guest
check "a symlink in place of the unit: err exists, the link and its target untouched" '[ $RC = 1 ] && has "err exists" && [ -L "$SYSD/$MU" ] && [ "$(cat "$TMP/elsewhere")" = x ]'
run remove --name nas
check "remove of that symlink: refused, nothing deleted" '[ $RC = 1 ] && [ -L "$SYSD/$MU" ] && [ -f "$TMP/elsewhere" ]'
reset; mkdir -p "$R/mnt/cyberdeck/nas"; add_guest
check "a mount point that exists and is not the helper's: err exists, the folder stays" '[ $RC = 1 ] && has "err exists" && [ -d "$R/mnt/cyberdeck/nas" ] && [ ! -e "$SYSD/$MU" ]'
reset; mkdir -p "$R/mnt"; ln -s "$TMP" "$R/mnt/cyberdeck"; add_guest
check "/mnt/cyberdeck is a symlink: err other, nothing written through it" '[ $RC = 1 ] && has "err other" && [ -z "$(ls -A "$TMP/nas" 2>/dev/null)" ] && [ ! -e "$TMP/nas" ]'
reset; mkdir -p "$R/etc/cyberdeckzero"; ln -s "$TMP" "$R/etc/cyberdeckzero/shares"; mkcred abcdef12 "$GOODCRED"
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "the credentials folder is a symlink: refused, nothing written through it" '[ $RC = 1 ] && [ ! -e "$TMP/nas.cred" ]'
reset; mkdir -p "$R/etc/cyberdeckzero/shares"; : > "$R/etc/cyberdeckzero/shares/nas.cred"
add_guest
check "a credentials file with that name is there: err exists" '[ $RC = 1 ] && has "err exists"'

# ---------------------------------------------------------------------------------------------------------------
# list
reset
add_guest
run add --name two --host fileserver --share Data --subfolder a/b
mkcred abcdef12 "$GOODCRED"; run add --name three --host 10.0.0.9 --share priv --user bob --credfile "$CRED"
printf '[Mount]\nWhere=/mnt/cyberdeck/foreign\n' > "$SYSD/mnt-cyberdeck-foreign.mount"
printf '%s evil\n# share: name=bad;x host=1.2.3.4 share=s auth=guest\n' "# cyberdeckzero-share v1: made by the launcher helper, remove with: cyberdeckzero-share remove --name" > "$SYSD/mnt-cyberdeck-bad.mount"
echo "$MU" > "$ST/active"
echo "mnt-cyberdeck-two.automount" >> "$ST/active"
echo "mnt-cyberdeck-three.mount" > "$ST/failed"
run list
check "list: the helper's shares with their state, subfolder only when there is one, foreign and malformed units skipped, ok last" '[ $RC = 0 ] && [ "$OUT" = "nas 192.168.1.5 music mounted
three 10.0.0.9 priv failed
two fileserver Data a/b idle
ok" ]'
check "list: the password is in no output" '! has s3cr3t'
reset; run list
check "list with no share: just ok" '[ $RC = 0 ] && [ "$OUT" = ok ]'

# ---------------------------------------------------------------------------------------------------------------
# remove
reset; add_guest; : > "$LOG"
run remove --name nas
check "remove: ok, units and mount point gone" '[ $RC = 0 ] && [ "$OUT" = ok ] && nothing_left'
check "remove: automount disabled and stopped, the mount stopped, then daemon-reload" 'grep -qF "systemctl disable --now $AU" "$LOG" && grep -qF "systemctl stop $MU" "$LOG" && grep -q "daemon-reload" "$LOG"'
run remove --name nas
check "remove again: ok, nothing to do (idempotent)" '[ $RC = 0 ] && [ "$OUT" = ok ] && nothing_left'
run remove --name neverthere
check "remove of a name that was never there: ok" '[ $RC = 0 ] && [ "$OUT" = ok ]'
reset; add_guest; echo "$R/mnt/cyberdeck/nas" > "$ST/mounted"; export FAKE_UMOUNT_BUSY=1; : > "$LOG"
run remove --name nas
check "remove of a busy mount: umount, then umount -l; units gone, ok" '[ $RC = 0 ] && grep -q "^umount $R/mnt/cyberdeck/nas" "$LOG" && grep -q "^umount -l $R/mnt/cyberdeck/nas" "$LOG" && [ ! -e "$SYSD/$MU" ]'
unset FAKE_UMOUNT_BUSY
reset; add_guest; : > "$R/mnt/cyberdeck/nas/keep"
run remove --name nas
check "remove: a mount point that is not empty is left (only an empty folder is removed), units gone, ok" '[ $RC = 0 ] && [ -f "$R/mnt/cyberdeck/nas/keep" ] && [ ! -e "$SYSD/$MU" ]'
reset; add_guest; run add --name keepme --host 1.2.3.4 --share s; run remove --name nas
check "remove: another share is not touched" '[ -f "$SYSD/mnt-cyberdeck-keepme.mount" ] && [ -d "$R/mnt/cyberdeck/keepme" ]'
reset; add_guest; run remove --name nas; add_guest
check "add after remove works again" '[ $RC = 0 ] && [ "$OUT" = "ok path=/mnt/cyberdeck/nas" ]'

# ---------------------------------------------------------------------------------------------------------------
# stdin is never read, a password on it is never used or echoed
reset; STDIN_DATA="STDIN-PASSWORD-QQ77"
add_guest
check "stdin with a password: add guest still ok, the mount's stdin empty" '[ $RC = 0 ] && [ ! -s "$ST/mount.stdin" ]'
reset; STDIN_DATA="STDIN-PASSWORD-QQ77"; mkcred abcdef12 "$GOODCRED"
run add --name nas --host 192.168.1.5 --share music --user bob --credfile "$CRED"
check "stdin with a password: add with a user takes the password from the file only" '[ $RC = 0 ] && [ "$(cat "$R/etc/cyberdeckzero/shares/nas.cred")" = "username=bob
password=$PWD1" ]'
check "stdin password: in no output, log, unit, file or stderr" '[ -z "$(grep -rlF STDIN-PASSWORD-QQ77 "$TMP" "$R" 2>/dev/null)" ] && ! has STDIN-PASSWORD'
STDIN_DATA="STDIN-PASSWORD-QQ77"; run list
check "stdin with a password: list does not echo it" '! has STDIN-PASSWORD'
run remove --name nas
check "stdin with a password: remove does not echo it" '[ $RC = 0 ] && ! has STDIN-PASSWORD'
STDIN_DATA="STDIN-PASSWORD-QQ77"; run add --name 'bad,name' --host 1.2.3.4 --share s
check "stdin with a password: an error reply does not echo it" '[ $RC = 2 ] && ! has STDIN-PASSWORD && ! grep -q STDIN-PASSWORD "$TMP/stderr"'
check "the helper is silent on stderr in the normal paths" '[ ! -s "$TMP/stderr" ] || ! grep -q . "$TMP/stderr"'

printf '\n%s passed, %s failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
