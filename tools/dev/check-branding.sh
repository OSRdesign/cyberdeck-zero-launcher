#!/bin/sh
# SPDX-License-Identifier: MIT
#
# check-branding.sh: fail when a tracked file names M5Stack or CardputerZero outside the allowed places.
#
#   tools/dev/check-branding.sh [--allow FILE] [REPO_ROOT]
#
#   Greps the tracked and untracked (not ignored) text files (git grep --untracked; submodules not entered) for the brand words M5Stack, M5,
#   CardputerZero, Cardputer Zero and M5CardputerZero (case-insensitive; "M5" only at the start of a word).
#   A hit is allowed when, after removing the allowed tokens below from the line, no brand word remains:
#     - SPDX-FileCopyrightText lines of M5Stack Technology CO LTD (MIT notice, must stay)
#     - names inherited from upstream that stay (NAMING.md): cp0_ APPLaunch APPLAUNCH_ M5APPSTORE_
#       .config/cardputerzero cardputerzero-appstore /com/cardputerzero/applaunch
#       M5CardputerZero-APPLaunch_fcntl.lock (lock file shared with legacy apps)
#     - the bundled Store (M5Stack's code, unchanged): M5CardputerZero-AppStore, "CardputerZero Hub",
#       cardputerzero.github.io cardputer-zero-repo
#     - the keymap file name tca8418_keypad_m5stack_keymap (inherited keyboard path)
#     - the approved credit and disclaimer phrases (report 025): "CardputerZero launcher by M5Stack",
#       "M5Stack's CardputerZero Store", "CardputerZero Store by M5Stack", "endorsed by M5Stack"
#   Since report 029 (2026-10-22) "stock CardputerZero", the stock Calculator's name, M5IOE1 and
#   /proc/cardputerzero_hw_id are no longer allowed: the code that used them is gone, write "legacy app" or
#   "default profile" instead.
#   Also allowed: lines between a "branding-allow-begin" and a "branding-allow-end" marker in the same file (put
#   them in a comment, e.g. <!-- branding-allow-begin: credits -->), for credits and derivation statements, and
#   paths listed in the allow-list (default: branding-allow.txt next to this script): one git pathspec glob per
#   line ("**" crosses directories), "#" starts a comment (the reason).
#   Prints "file:line: text" for every other occurrence and exits 1 if there is one, 0 otherwise.
#
#   REPO_ROOT defaults to the git top level of the current directory. The same script serves the apps repo with
#   its own allow-list (--allow FILE, or the file next to the script). GIT=... picks the git command (default git;
#   git.exe is used automatically in WSL when Linux git cannot read a worktree made on Windows).
set -eu

here=$(cd -- "$(dirname -- "$0")" && pwd)
allow=$here/branding-allow.txt
root=
while [ $# -gt 0 ]; do
    case $1 in
        -h | --help) awk 'NR > 3 && /^#/ { sub(/^# ?/, ""); print; next } NR > 3 { exit }' "$0"; exit 0 ;;
        --allow) allow=${2:?--allow needs a file}; shift 2 ;;
        -*) echo "unknown option $1 (see -h)" >&2; exit 2 ;;
        *) root=$1; shift ;;
    esac
done
case $allow in /*) ;; *) allow=$(pwd)/$allow ;; esac
[ -n "$root" ] || root=$(git rev-parse --show-toplevel)
cd -- "$root"
[ -r "$allow" ] || { echo "allow-list not found: $allow" >&2; exit 2; }

# In WSL, a git worktree created on Windows points to a C:/ path that Linux git cannot open: use git.exe then.
GIT=${GIT:-git}
if ! "$GIT" rev-parse --git-dir >/dev/null 2>&1; then
    if command -v git.exe >/dev/null 2>&1 && git.exe rev-parse --git-dir >/dev/null 2>&1; then
        GIT=git.exe
    else
        echo "not a git checkout (or git cannot read it): $root" >&2
        exit 2
    fi
fi

# Pathspecs: everything, minus the allow-listed globs (comments and blanks stripped).
set -- .
while IFS= read -r line || [ -n "$line" ]; do
    line=${line%%#*}
    line=$(printf '%s' "$line" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
    [ -n "$line" ] || continue
    set -- "$@" ":(exclude,glob)$line"
done < "$allow"

words='(^|[^a-z0-9])m5|cardputer[ _-]?zero'
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

git_grep() { "$GIT" -c core.quotePath=false grep "$@" || [ $? -eq 1 ]; }   # exit 1 = no match: fine

git_grep --untracked -n -I -i -E "$words" -- "$@" > "$tmp/hits"

# Lines inside branding-allow-begin/end blocks, as "file:line".
echo "-:0" > "$tmp/blocks"   # never empty: the awk below reads it as its first file
git_grep --untracked -l -I -F 'branding-allow-begin' -- "$@" > "$tmp/marked"
while IFS= read -r f; do
    awk -v f="$f" '/branding-allow-end/ { on = 0 } on { print f ":" NR } /branding-allow-begin/ { on = 1 }' "$f" >> "$tmp/blocks"
done < "$tmp/marked"

awk -v words="$words" '
FNR == NR { inblock[$0] = 1; next }
{
    i = index($0, ":"); rest = substr($0, i + 1); j = index(rest, ":")
    loc = substr($0, 1, i + j - 1); text = substr(rest, j + 1)
    if (loc in inblock) next
    if (text ~ /SPDX-FileCopyrightText:.*M5Stack Technology CO LTD/) next
    t = tolower(text)
    n = split("m5cardputerzero-appstore|m5cardputerzero-applaunch_fcntl.lock|" \
              "m5appstore_|cardputerzero-appstore|.config/cardputerzero|" \
              "/com/cardputerzero/applaunch|cardputerzero hub|cardputerzero.github.io|cardputer-zero-repo|" \
              "tca8418_keypad_m5stack_keymap|" \
              "cardputerzero launcher by m5stack|m5stack\047s cardputerzero store|cardputerzero store by m5stack|" \
              "endorsed by m5stack|applaunch_|applaunch|cp0_", tok, "|")
    for (k = 1; k <= n; k++)
        while ((p = index(t, tok[k])) > 0) t = substr(t, 1, p - 1) " " substr(t, p + length(tok[k]))
    if (t ~ words) print loc ": " text
}' "$tmp/blocks" "$tmp/hits" > "$tmp/bad"

if [ -s "$tmp/bad" ]; then
    cat "$tmp/bad"
    echo "branding: $(wc -l < "$tmp/bad" | tr -d ' ') line(s) name M5Stack/CardputerZero outside the allowed places (see -h; allow-list $allow)" >&2
    exit 1
fi
echo "branding: OK"
