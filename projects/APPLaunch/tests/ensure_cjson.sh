#!/bin/sh
# SPDX-License-Identifier: MIT
#
# ensure_cjson.sh [SDK_DIR]: make sure SDK/github_source/cJSON (the SDK's cjson component, MIT; licence in
# LICENSES/cJSON-MIT.txt) is there for the PC tests and the render harness, which compile it without scons. The launcher's
# scons build downloads it itself (CONFIG_CJSON_ENABLED, check_component); a build tree seeded from another tree may not
# have it yet. Same source and commit as the SDK (SDK/github_source/source-list.sh), same layout as the SDK's download
# (the archive of that commit, unpacked as github_source/cJSON). Prints the cJSON folder.
set -eu
sdk=${1:-$(CDPATH= cd -- "$(dirname "$0")/../../../SDK" && pwd)}
dest="$sdk/github_source/cJSON"
if [ -f "$dest/cJSON.c" ] && [ -f "$dest/cJSON.h" ]; then
    echo "$dest"
    exit 0
fi
list="$sdk/github_source/source-list.sh"
commit=$(sed -n 's|^git_clone_and_checkout_commit https://github.com/DaveGamble/cJSON.git *\([0-9a-f]\{40\}\).*|\1|p' "$list" | head -n 1)
[ -n "$commit" ] || { echo "ensure_cjson: no cJSON line in $list" >&2; exit 2; }
python3 -I - "$commit" "$dest" <<'EOF'
import io, os, shutil, sys, tempfile, urllib.request, zipfile
commit, dest = sys.argv[1], sys.argv[2]
url = "https://github.com/DaveGamble/cJSON/archive/%s.zip" % commit
data = urllib.request.urlopen(url, timeout=60).read()
tmp = tempfile.mkdtemp(dir=os.path.dirname(dest))
try:
    zipfile.ZipFile(io.BytesIO(data)).extractall(tmp)
    top = os.path.join(tmp, "cJSON-" + commit)
    if not os.path.isfile(os.path.join(top, "cJSON.c")):
        sys.exit("ensure_cjson: the archive has no cJSON.c")
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.rename(top, dest)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
EOF
echo "$dest"
