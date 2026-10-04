#!/usr/bin/env python3
"""Check that the copies of the launcher's top bar (cp0_statusbar.[ch]) in the apps repo have not drifted.

    python check_statusbar_drift.py [--apps-repo PATH]

The launcher owns ext_components/cp0_lvgl/{include,src/cp0}/cp0_statusbar.[ch]. Apps that draw the same bar keep a copy
(today: apps/viz1090/build). Line endings (CRLF / LF) are ignored. The apps repo is found next to the launcher repo
(../cyberdeck-zero-apps), or with --apps-repo, or with the APPS_REPO environment variable.

Exit code: 0 identical, 1 a copy differs (a unified diff is printed), 2 a file or the apps repo is missing.
"""
import argparse
import difflib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.abspath(os.path.join(HERE, "..", "..", ".."))                  # docs/dev/deck -> repo root
SOURCES = {                                                                      # file -> path in the launcher repo
    "cp0_statusbar.h": "ext_components/cp0_lvgl/include/cp0_statusbar.h",
    "cp0_statusbar.c": "ext_components/cp0_lvgl/src/cp0/cp0_statusbar.c",
}
COPIES = ["apps/viz1090/build"]                                                  # folders in the apps repo holding copies


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "\n").split("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apps-repo", default=os.environ.get("APPS_REPO") or os.path.join(LAUNCHER, "..", "cyberdeck-zero-apps"))
    apps = os.path.abspath(ap.parse_args().apps_repo)
    if not os.path.isdir(apps):
        print("apps repo not found: %s (use --apps-repo or APPS_REPO)" % apps)
        return 2
    missing, differ = 0, 0
    for name, rel in SOURCES.items():
        src = os.path.join(LAUNCHER, rel)
        if not os.path.isfile(src):
            print("MISSING launcher file: %s" % src)
            missing += 1
            continue
        want = read(src)
        for folder in COPIES:
            copy = os.path.join(apps, folder, name)
            label = "%s/%s" % (folder, name)
            if not os.path.isfile(copy):
                print("MISSING copy: %s" % copy)
                missing += 1
            elif read(copy) == want:
                print("ok      %s" % label)
            else:
                differ += 1
                print("DRIFT   %s differs from the launcher's %s" % (label, rel))
                sys.stdout.writelines(l + "\n" for l in difflib.unified_diff(want, read(copy), "launcher/" + rel, label, lineterm="", n=2))
    if missing:
        return 2
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
