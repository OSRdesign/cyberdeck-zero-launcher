#!/usr/bin/env python3
"""Check that the copies of the launcher's top bar (cp0_statusbar.[ch]) in the apps repo have not drifted.

    python check_statusbar_drift.py [--apps-repo PATH]

The launcher owns ext_components/cp0_lvgl/{include,src/cp0}/cp0_statusbar.[ch]. Apps that draw the same bar without
LVGL's cp0_lvgl component keep a copy (COPIES: viz1090's display bridge). Apps that link cp0_lvgl (their
SConstruct REQUIREMENTS list 'cp0_lvgl') use the shared cp0::ui::StatusStrip instead and have no copy at all: for them
"no copy" is the expected state, and a copy is reported as one to delete. Copies found anywhere else under apps/ are
compared too and reported as not listed. Line endings (CRLF / LF) are ignored. The apps repo is found next to the
launcher repo (../cyberdeck-zero-apps), or with --apps-repo, or with the APPS_REPO environment variable.

Exit code: 0 identical (or no copy where the app links cp0_lvgl), 1 a copy differs, is not listed, or sits in an app
that links cp0_lvgl (a unified diff is printed for a difference), 2 a file or the apps repo is missing.
"""
import argparse
import difflib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.abspath(os.path.join(HERE, "..", "..", ".."))                  # docs/dev/deck -> repo root
SOURCES = {                                                                      # file -> path in the launcher repo
    "cp0_statusbar.h": "ext_components/cp0_lvgl/include/cp0_statusbar.h",
    "cp0_statusbar.c": "ext_components/cp0_lvgl/src/cp0/cp0_statusbar.c",
}
COPIES = [                                                                       # folders in the apps repo holding copies
    "apps/viz1090/build",
]
SKIP_DIRS = {".git", "SDK", "github_source", "dist", "node_modules"}           # never app sources
LINKS_CP0_LVGL = re.compile(r"""REQUIREMENTS\s*\+?=\s*\[[^\]]*['"]cp0_lvgl['"]""")


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "\n").split("\n")


def app_of(folder):
    """apps/<id>/... -> apps/<id> (None outside apps/)."""
    parts = folder.replace("\\", "/").split("/")
    return "/".join(parts[:2]) if len(parts) >= 2 and parts[0] == "apps" else None


def links_cp0_lvgl(apps, app):
    """True when one of the app's SConstruct files lists cp0_lvgl in its REQUIREMENTS."""
    root = os.path.join(apps, app)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        if "SConstruct" in filenames:
            with open(os.path.join(dirpath, "SConstruct"), "rb") as f:
                text = f.read().decode("utf-8", "replace")
            # comments do not count: "# no cp0_lvgl runtime" is not a requirement
            if LINKS_CP0_LVGL.search("\n".join(line.split("#", 1)[0] for line in text.split("\n"))):
                return True
    return False


def found_copies(apps):
    """Every apps/... folder holding a cp0_statusbar.h or .c (relative, forward slashes)."""
    found = set()
    root = os.path.join(apps, "apps")
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        if any(name in filenames for name in SOURCES):
            found.add(os.path.relpath(dirpath, apps).replace("\\", "/"))
    return found


def compare(label, copy, rel, want):
    """0 identical, 1 differs (diff printed)."""
    got = read(copy)
    if got == want:
        print("ok      %s" % label)
        return 0
    print("DRIFT   %s differs from the launcher's %s" % (label, rel))
    sys.stdout.writelines(l + "\n" for l in difflib.unified_diff(want, got, "launcher/" + rel, label, lineterm="", n=2))
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apps-repo", default=os.environ.get("APPS_REPO") or os.path.join(LAUNCHER, "..", "cyberdeck-zero-apps"))
    apps = os.path.abspath(ap.parse_args().apps_repo)
    if not os.path.isdir(apps):
        print("apps repo not found: %s (use --apps-repo or APPS_REPO)" % apps)
        return 2
    missing, bad = 0, 0
    wants = {}
    for name, rel in SOURCES.items():
        src = os.path.join(LAUNCHER, rel)
        if not os.path.isfile(src):
            print("MISSING launcher file: %s" % src)
            missing += 1
        else:
            wants[name] = read(src)
    if missing:
        return 2

    linked = {}
    for folder in sorted(set(COPIES) | found_copies(apps)):
        app = app_of(folder)
        if app not in linked:
            linked[app] = bool(app) and links_cp0_lvgl(apps, app)
        listed = folder in COPIES
        for name, rel in SOURCES.items():
            copy = os.path.join(apps, folder, name)
            label = "%s/%s" % (folder, name)
            if linked[app]:
                if os.path.isfile(copy):
                    bad += 1
                    print("COPY    %s: %s links cp0_lvgl and must use cp0::ui::StatusStrip, delete the copy" % (label, app))
                else:
                    print("ok      %s: no copy, %s links cp0_lvgl (shared StatusStrip)" % (label, app))
                continue
            if not os.path.isfile(copy):
                if listed:
                    print("MISSING copy: %s" % copy)
                    missing += 1
                continue
            if not listed:
                bad += 1
                print("UNLISTED %s: a copy not in COPIES (add it to check_statusbar_drift.py)" % label)
            bad += compare(label, copy, rel, wants[name])
    if missing:
        return 2
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
