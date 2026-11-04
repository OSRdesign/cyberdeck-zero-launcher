# shellcheck shell=bash
# SPDX-License-Identifier: MIT
#
# Shared helpers of tools/dev/*.sh (sourced, not run). See README.md.
#
#   in_wsl                 true inside WSL (Linux with "microsoft" in /proc/version)
#   reexec_in_wsl SCRIPT ARGS...   from Git Bash on Windows: run the same script inside WSL (path converted to /mnt/x/...)
#   checkout_root          the checkout this script lives in (tools/dev/../..)
#   default_build_dir      ~/zero7/<checkout folder name>: one WSL build tree per checkout / worktree
#   take_build_lock        flock /tmp/wsl-build.lock on fd 9 (one build or test run at a time on the build box)
#   sync_tree SRC DST      copy the checkout into the WSL build tree (see the comment on the function)

DEV_LOCK=/tmp/wsl-build.lock
DEV_SEED=${DEV_SEED:-$HOME/zero7/launcher}   # WSL tree whose SDK/ (downloaded LVGL and libraries) seeds a new build dir

in_wsl() {
    [[ -r /proc/version ]] && grep -qi microsoft /proc/version
}

# Git Bash path (/c/x/y) or Windows path (C:/x/y, C:\x\y) -> WSL path (/mnt/c/x/y)
to_wsl_path() {
    local p=$1
    p=${p//\\//}
    if [[ $p =~ ^/([a-zA-Z])/(.*)$ ]]; then
        printf '/mnt/%s/%s\n' "${BASH_REMATCH[1],,}" "${BASH_REMATCH[2]}"
    elif [[ $p =~ ^([a-zA-Z]):/(.*)$ ]]; then
        printf '/mnt/%s/%s\n' "${BASH_REMATCH[1],,}" "${BASH_REMATCH[2]}"
    else
        printf '%s\n' "$p"
    fi
}

# Not in WSL (Git Bash on the Windows dev machine): run the same script in WSL and exit with its status.
# MSYS_NO_PATHCONV keeps Git Bash from rewriting the /mnt/... argument into a Windows path.
reexec_in_wsl() {
    local script=$1
    shift
    in_wsl && return 0
    command -v wsl.exe >/dev/null 2>&1 || {
        echo "not in WSL and wsl.exe not found: run this script inside WSL" >&2
        exit 2
    }
    local abs
    abs=$(cd -- "$(dirname -- "$script")" && pwd)/$(basename -- "$script")
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' exec wsl.exe -e bash "$(to_wsl_path "$abs")" "$@"
}

checkout_root() {
    (cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
}

default_build_dir() {
    printf '%s/zero7/%s\n' "$HOME" "$(basename -- "$(checkout_root)")"
}

take_build_lock() {
    exec 9>"$DEV_LOCK"
    if ! flock -n 9; then
        echo "waiting for $DEV_LOCK (another build or test run holds it)..."
        flock 9
    fi
}

# sync_tree SRC DST: make DST (the WSL build tree) a copy of the checkout SRC.
#   - build products are never copied nor deleted: build/, dist/, .git, the render harness out/, the Pi bundle dir and
#     tarball, __pycache__;
#   - projects/APPLaunch/, ext_components/, LICENSES/ and tools/strokes/ are synced with --delete, so a file removed or never
#     present on this branch (another branch's new page, for example; the recogniser files that moved from tools/strokes to
#     ext_components/cp0_lvgl in report 041) cannot end up in the build (scons globs ui/settings/*);
#   - SDK/ (2+ GB of downloaded LVGL and libraries; empty in a fresh git worktree) is copied once from $DEV_SEED when
#     DST has none, so a new worktree builds without a network download. Never shared: each tree has its own copy.
#     projects/AppStore (submodule, empty in a fresh worktree) is seeded the same way from $DEV_SEED.
# Call it with the build lock held.
sync_tree() {
    local src=$1 dst=$2 d
    local excludes=(--exclude build/ --exclude dist/ --exclude .git --exclude __pycache__/
        --exclude /projects/APPLaunch/tests/render/out/ --exclude /tests/render/out/
        --exclude /projects/APPLaunch/pizero2w/bundle/ --exclude /pizero2w/bundle/
        --exclude /projects/APPLaunch/pizero2w/pizero2w-bundle.tar.gz --exclude /pizero2w/pizero2w-bundle.tar.gz
        --exclude '/projects/APPLaunch/pizero2w/cyberdeckzero-launcher-*.tar.gz' --exclude '/pizero2w/cyberdeckzero-launcher-*.tar.gz')
    mkdir -p "$dst"
    if [[ ! -d $dst/SDK/github_source && -d $DEV_SEED/SDK/github_source && $dst != "$DEV_SEED" ]]; then
        echo "seeding $dst/SDK from $DEV_SEED/SDK (one time, about 2 GB)"
        mkdir -p "$dst/SDK"
        rsync -a "$DEV_SEED/SDK/" "$dst/SDK/"
    fi
    # projects/AppStore is a submodule (empty in a fresh worktree); the app-unit registry-mirror test and the --with-store
    # build need its sources: copy them once from $DEV_SEED when neither the checkout nor DST has them.
    if [[ ! -d $src/projects/AppStore/main && ! -d $dst/projects/AppStore/main && -d $DEV_SEED/projects/AppStore/main         && $dst != "$DEV_SEED" ]]; then
        echo "seeding $dst/projects/AppStore from $DEV_SEED"
        mkdir -p "$dst/projects/AppStore"
        rsync -a --exclude .git --exclude build/ "$DEV_SEED/projects/AppStore/" "$dst/projects/AppStore/"
    fi
    local copied
    copied=$(mktemp)
    rsync -a --out-format='%n' "${excludes[@]}" "$src/" "$dst/" >>"$copied"
    for d in projects/APPLaunch ext_components LICENSES tools/strokes; do
        rsync -a --delete --out-format="$d/%n" "${excludes[@]}" "$src/$d/" "$dst/$d/" >>"$copied"
    done
    strip_crlf "$src" "$dst" <"$copied"
    rm -f "$copied"
}

# strip_crlf SRC DST < list: a Windows checkout (core.autocrlf=true) has CRLF text files, which break bash scripts and text
# goldens in WSL. For each path just copied (relative to DST), turn CRLF into LF when it is a text file: a known text
# extension, no extension, or a #! script, and grep -I does not see it as binary. PNG, fonts, archives, audio are never
# touched; a file already LF is left as is (no rewrite). The converted file keeps the source mtime, so a sync that copies
# it again (size differs from the CRLF source) leaves it unchanged for make.
strip_crlf() {
    local src=$1 dst=$2 rel f
    while IFS= read -r rel; do
        f=$dst/$rel
        [[ -f $f && ! -L $f ]] || continue
        case ${rel##*/} in
            *.sh|*.bash|*.py|*.c|*.h|*.cc|*.cpp|*.hpp|*.inc|*.txt|*.md|*.json|*.scene|*.mk|*.rules|*.service|*.desktop|\
            *.tsv|*.csv|*.cmake|*.conf|*.cfg|*.ini|*.yml|*.yaml|*.toml|*.dts|*.headers|*.snippet|*.gitignore|*.gitattributes|\
            *.gitmodules|*.log|.clang-format) ;;
            *.*) [[ $(head -c2 "$f" | tr -d '\0') == '#!' ]] || continue ;;
            *) ;;
        esac
        if grep -q $'\r' "$f" && grep -Iq . "$f"; then
            sed -i 's/\r$//' "$f" && touch -r "$src/$rel" "$f"
        fi
    done
    return 0
}
