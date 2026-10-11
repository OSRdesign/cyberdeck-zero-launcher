#!/bin/sh
# Offer the project's app source in Settings > Apps, once per user.
#
# OSRdesign/cyberdeck-zero-apps (LAN Scan, viz1090, Wi-Fi Survey, Mesh Hop, ...) is added to the Store's list of
# sources when the user has no source of their own yet, then synced once, so Settings > Apps > Available lists its
# apps. install.sh runs it as the launcher user. By hand: run it as that user, never with sudo (no password needed):
#     sh default-sources.sh [--dry-run] [--no-sync]
#
# The list is the Store backend's file ~/.local/share/cardputerzero-appstore/registries.json (M5APPSTORE_STATE_DIR
# moves the folder, as for the Store). It is written in the Store's own format (what the Store writes after adding a
# source) and only when it is missing or holds no source besides the built-in CardputerZero Hub; a file that was there
# is kept as registries.json.bak-applaunch. A list with sources of the user's own is never changed.
# The decision is taken once: the marker applaunch-default-source in the same folder records it, so a source the user
# removes is not added back by the next install.sh. uninstall.sh --purge-config deletes the folder, marker included.
#
# Options:
#   --dry-run   print what would be done, change nothing
#   --no-sync   only write the list (Settings > Apps > Sources > Sync my sources fetches the apps later)
#   -h, --help  this text
#
# APPLAUNCH_APPSTORE_BIN replaces the Store backend used for the sync (as for the launcher; a test hook too).
set -eu

DRY=0
SYNC=1
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY=1 ;;
        --no-sync) SYNC=0 ;;
        -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

NAME=OSRdesign/cyberdeck-zero-apps
# what Settings > Apps > Sources > Add makes of "OSRdesign/cyberdeck-zero-apps" (apps_backend::normalize_source)
URL=https://raw.githubusercontent.com/$NAME/main/registry.json
# the Store's built-in registries (projects/AppStore/main/backend/appstore_native.cpp: kDefaultUrl, kCnUrl)
HUB_URL=https://cardputerzero.github.io/generated/registry.json
HUB_NAME="CardputerZero Hub"
HUB_CN_URL=https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json
HUB_CN_NAME="CardputerZero Hub CN"
BACKEND=${APPLAUNCH_APPSTORE_BIN:-/usr/share/APPLaunch/bin/M5CardputerZero-AppStore}
TAB=$(printf '\t')

say() { printf 'default app source: %s\n' "$*"; }

if [ "$(id -u)" -eq 0 ]; then
    echo "default app source: run this as the launcher user, not as root (install.sh does that itself)" >&2
    exit 2
fi
[ -n "${HOME:-}" ] || { echo "default app source: HOME is not set" >&2; exit 2; }
STATE_DIR=${M5APPSTORE_STATE_DIR:-$HOME/.local/share/cardputerzero-appstore}
case "$STATE_DIR" in "~/"*) STATE_DIR="$HOME/${STATE_DIR#??}" ;; esac   # the Store expands "~/" the same way
CONFIG="$STATE_DIR/registries.json"
MARKER="$STATE_DIR/applaunch-default-source"

if [ -e "$MARKER" ]; then
    say "already decided ($(head -n 1 "$MARKER" 2>/dev/null || true)): the list is left as it is"
    exit 0
fi

# The sources in the list other than the built-in hub (the Store skips empty URLs and adds https:// when there is no
# scheme; it writes one "url" per source).
custom_urls() {
    [ -r "$CONFIG" ] || return 0
    grep -o '"url"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG" 2>/dev/null \
        | sed -e 's/^"url"[[:space:]]*:[[:space:]]*"//' -e 's/"$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
        | grep -v '^$' | sed '/:\/\//!s|^|https://|' \
        | grep -v -x -F -e "$HUB_URL" -e "$HUB_CN_URL" || true
}
# A top-level string value of the file as the Store writes it (two-space indent), e.g. "region".
top_value() {
    [ -r "$CONFIG" ] || return 0
    sed -n "s/^  \"$1\"[[:space:]]*:[[:space:]]*\"\([A-Za-z]*\)\",\{0,1\}[[:space:]]*\$/\1/p" "$CONFIG" | head -n 1
}
mark() { printf '%s %s\n' "$(date +%Y-%m-%d)" "$1" > "$MARKER"; }

CUSTOM=$(custom_urls)
if [ -n "$CUSTOM" ]; then
    COUNT=$(printf '%s\n' "$CUSTOM" | wc -l | tr -d ' ')
    if printf '%s\n' "$CUSTOM" | grep -q -i -F "/$NAME/"; then
        RESULT="$NAME is already in the list"
    else
        RESULT="not added: the user's own sources ($COUNT) are kept"
    fi
    if [ "$DRY" = 1 ]; then say "$RESULT (dry run: nothing written)"; exit 0; fi
    mark "$RESULT"
    say "$RESULT"
    exit 0
fi

# Region of the built-in hub: kept from the file when it has one (the Store's own rules, load_config()).
REGION=$(top_value region)
case "$REGION" in auto|default|CN) ;; *) REGION=auto ;; esac
ACTIVE=$(top_value active_region)
[ "$ACTIVE" = CN ] || ACTIVE=default
if [ "$ACTIVE" = CN ]; then
    B_NAME=$HUB_CN_NAME; B_URL=$HUB_CN_URL; B_REGION=CN
else
    B_NAME=$HUB_NAME; B_URL=$HUB_URL; B_REGION=default
fi
# Exactly what the Store writes (nlohmann::json, keys sorted, two-space indent): the hub first, then the source.
seed_json() {
    cat <<EOF
{
  "active_region": "$ACTIVE",
  "region": "$REGION",
  "registries": [
    {
      "builtin": true,
      "enabled": true,
      "name": "$B_NAME",
      "region": "$B_REGION",
      "url": "$B_URL"
    },
    {
      "enabled": true,
      "name": "$NAME",
      "url": "$URL"
    }
  ]
}
EOF
}

if [ "$DRY" = 1 ]; then
    say "would add $NAME to $CONFIG$( [ "$SYNC" = 1 ] && echo ' and sync it')$( [ -s "$CONFIG" ] && echo " (the current file kept as registries.json.bak-applaunch)")"
    seed_json
    exit 0
fi

mkdir -p "$STATE_DIR"
if [ -s "$CONFIG" ] && [ ! -e "$CONFIG.bak-applaunch" ]; then cp -p "$CONFIG" "$CONFIG.bak-applaunch"; fi
seed_json > "$CONFIG.applaunch-tmp"
mv "$CONFIG.applaunch-tmp" "$CONFIG"
mark "added $NAME"
say "added $NAME to $CONFIG"

LATER="Settings > Apps > Sources > Sync my sources fetches its apps"
if [ "$SYNC" = 0 ]; then say "not synced (--no-sync): $LATER"; exit 0; fi
if [ ! -x "$BACKEND" ]; then say "the Store backend $BACKEND is not installed: not synced ($LATER once it is)"; exit 0; fi

# One sync of this source only, as Settings > Apps does it (settings_apps_jobs.cpp, task_sync).
TIMEOUT=""
command -v timeout >/dev/null 2>&1 && TIMEOUT="timeout 120"
RC=0
OUT=$($TIMEOUT "$BACKEND" --edit-registry "$URL" "$URL" --registry-name "$NAME" 2>&1 </dev/null) || RC=$?
DONE=$(printf '%s\n' "$OUT" | grep "^REGISTRY${TAB}UPDATED${TAB}" | head -n 1 || true)
if [ "$RC" = 0 ] && [ -n "$DONE" ]; then
    STATUS=$(printf '%s\n' "$DONE" | cut -f5)
    APPS=$(printf '%s\n' "$DONE" | cut -f6)
    say "synced: ${APPS:-?} apps ($STATUS)"
else
    if [ "$RC" = 124 ]; then
        WHY="timed out"
    else
        WHY=$(printf '%s\n' "$OUT" | grep "^ERROR${TAB}" | tail -n 1 | cut -f2)
        WHY=${WHY:-backend exit $RC}
    fi
    say "could not sync it now ($WHY): $LATER"
fi
exit 0
