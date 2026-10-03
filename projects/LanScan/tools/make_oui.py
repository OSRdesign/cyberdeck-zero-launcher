#!/usr/bin/env python3
"""Builds APPLaunch/share/oui.tsv (MAC prefix -> vendor) from the IEEE OUI registry.

    python3 tools/make_oui.py            # downloads https://standards-oui.ieee.org/oui/oui.csv
    python3 tools/make_oui.py oui.csv    # or use a local copy
"""
import csv, io, re, sys, urllib.request

URL = "https://standards-oui.ieee.org/oui/oui.csv"
OUT = "APPLaunch/share/oui.tsv"

def short(name):
    name = re.sub(r"\s+", " ", name).strip()
    # drop corporate suffixes to keep names short on a small screen
    name = re.sub(r"[,.]?\s+(Co\.?,? ?Ltd\.?|Corporation|Corp\.?|Incorporated|Inc\.?|GmbH|LLC|Ltd\.?|Limited|S\.?A\.?|B\.?V\.?|AG|PLC|Pty|Company)\b.*$", "", name, flags=re.I)
    return name[:30].strip(" ,.")

if len(sys.argv) > 1:
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
else:
    request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0 (cyberdeck-zero-launcher)"})
    text = urllib.request.urlopen(request, timeout=60).read().decode("utf-8", errors="replace")

rows = {}
for row in csv.reader(io.StringIO(text)):
    if len(row) >= 3 and re.fullmatch(r"[0-9A-Fa-f]{6}", row[1]):
        rows[row[1].upper()] = short(row[2])
with open(OUT, "w", encoding="utf-8", newline="\n") as out:
    for prefix in sorted(rows):
        out.write(f"{prefix}\t{rows[prefix]}\n")
print(len(rows), "prefixes written to", OUT)
