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
#   - projects/APPLaunch/ and ext_components/ are synced with --delete, so a file removed or never present on this
#     branch (another branch's new page, for example) cannot end up in the build (scons globs ui/settings/*);
#   - SDK/ (2+ GB of downloaded LVGL and libraries; empty in a fresh git worktree) is copied once from $DEV_SEED when
#     DST has none, so a new worktree builds without a network download. Never shared: each tree has its own copy.
# Call it with the build lock held.
sync_tree() {
    local src=$1 dst=$2 d
    local excludes=(--exclude build/ --exclude dist/ --exclude .git --exclude __pycache__/
        --exclude /projects/APPLaunch/tests/render/out/ --exclude /tests/render/out/
        --exclude /projects/APPLaunch/pizero2w/bundle/ --exclude /pizero2w/bundle/
        --exclude /projects/APPLaunch/pizero2w/pizero2w-bundle.tar.gz --exclude /pizero2w/pizero2w-bundle.tar.gz)
    mkdir -p "$dst"
    if [[ ! -d $dst/SDK/github_source && -d $DEV_SEED/SDK/github_source && $dst != "$DEV_SEED" ]]; then
        echo "seeding $dst/SDK from $DEV_SEED/SDK (one time, about 2 GB)"
        mkdir -p "$dst/SDK"
        rsync -a "$DEV_SEED/SDK/" "$dst/SDK/"
    fi
    rsync -a "${excludes[@]}" "$src/" "$dst/"
    for d in projects/APPLaunch ext_components; do
        rsync -a --delete "${excludes[@]}" "$src/$d/" "$dst/$d/"
    done
}
