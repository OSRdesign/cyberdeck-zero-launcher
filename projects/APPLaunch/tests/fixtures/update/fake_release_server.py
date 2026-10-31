#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# fake_release_server.py: a local stand-in for GitHub's release list and its downloads, for Settings > Updates (report
# 047 P1 and P2). The launcher asks it instead of GitHub when APPLAUNCH_UPDATE_URL points here (PC tests:
# test_settings_update_fetch.cpp, test_settings_update_download.cpp; a board: set
# APPLAUNCH_UPDATE_URL=http://<pc>:<port>/releases in the launcher's environment and restart it).
#
#   python3 fake_release_server.py [--port N] [--bind ADDR] [--file FILE] [--bundles DIR]
#
#   GET /releases      FILE (default releases-0.8.13.json next to this script) with an ETag; 304 when If-None-Match
#                      matches; X-RateLimit-Remaining: 59.
#                      With --bundles DIR: a release list made from the bundles in DIR instead (P2): one release per
#                      version of the files cyberdeckzero-launcher-<X.Y.Z>-<arch>.tar.gz there, newest first, each with
#                      its bundles, a .sha256 asset per bundle and GitHub's sha256 digest, the download addresses on
#                      this server (the Host the client asked, so a board on the LAN gets the PC's address), the body
#                      DIR/release-notes-<X.Y.Z>.md when it exists, else a short note with the end marker. A version with
#                      a "-suffix" is marked as a pre-release (never offered). The list is read again on each request:
#                      drop a new bundle in DIR and check again.
#   GET /download/NAME a bundle of DIR (or NAME.sha256: "<hash> <NAME>"), with byte ranges (curl -C - resumes: 206, or
#                      416 when the range starts at the end); ?rate=KIB limits it to KIB KiB/s (to see the progress
#                      and to stop a download on a fast network)
#   GET /ratelimit     403 with GitHub's rate limit body and headers (X-RateLimit-Remaining: 0, a reset time)
#   GET /error         500
#   GET /notjson       200 with a body that is no release list
#   GET /moved         302 to /releases (curl follows it)
#   GET /log           the If-None-Match, User-Agent and Range of every request so far, one per line
#
# Prints "port N" once it listens (N: the port, an ephemeral one with --port 0). Ctrl+C or SIGTERM stops it.
import argparse
import datetime
import hashlib
import http.server
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUNDLE = re.compile(r"^cyberdeckzero-launcher-([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)-(arm64|amd64)\.tar\.gz$")


def version_key(version):
    core, _, pre = version.partition("-")
    return (tuple(int(p) for p in core.split(".")), 0 if pre else 1, pre)


class Bundles:
    """The bundles of a folder and their sha256, computed again only when a file changes."""

    def __init__(self, folder):
        self.folder = folder
        self.hashes = {}

    def sha256(self, name):
        path = os.path.join(self.folder, name)
        st = os.stat(path)
        key = (st.st_size, st.st_mtime_ns)
        cached = self.hashes.get(name)
        if cached and cached[0] == key:
            return cached[1]
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        self.hashes[name] = (key, h.hexdigest())
        return self.hashes[name][1]

    def names(self):
        return sorted(n for n in os.listdir(self.folder) if BUNDLE.match(n))

    def release_list(self, base):
        versions = {}
        for name in self.names():
            versions.setdefault(BUNDLE.match(name).group(1), []).append(name)
        out = []
        for version in sorted(versions, key=version_key, reverse=True):
            assets = []
            newest = 0
            for name in versions[version]:
                path = os.path.join(self.folder, name)
                digest = self.sha256(name)
                newest = max(newest, os.stat(path).st_mtime)
                line = ("%s %s\n" % (digest, name)).encode()
                assets.append({"name": name, "size": os.stat(path).st_size, "digest": "sha256:" + digest,
                               "browser_download_url": "%s/download/%s" % (base, name)})
                assets.append({"name": name + ".sha256", "size": len(line),
                               "digest": "sha256:" + hashlib.sha256(line).hexdigest(),
                               "browser_download_url": "%s/download/%s.sha256" % (base, name)})
            notes = os.path.join(self.folder, "release-notes-%s.md" % version)
            if os.path.exists(notes):
                with open(notes, encoding="utf-8") as f:
                    body = f.read()
            else:
                body = ("**New since the last test release**\r\n\r\n- A test build of %s served by "
                        "fake_release_server.py.\r\n\r\n<!-- launcher-notes-end -->\r\n" % version)
            when = datetime.datetime.fromtimestamp(newest, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
            out.append({"tag_name": "v" + version, "name": "v" + version, "draft": False,
                        "prerelease": "-" in version, "published_at": when, "body": body, "assets": assets})
        return json.dumps(out, indent=1).encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8047)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--file", default=os.path.join(HERE, "releases-0.8.13.json"))
    parser.add_argument("--bundles", default="")
    args = parser.parse_args()
    with open(args.file, "rb") as f:
        fixed_releases = f.read()
    with open(os.path.join(HERE, "rate-limit.json"), "rb") as f:
        rate_limit = f.read()
    bundles = Bundles(args.bundles) if args.bundles else None
    log = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body=b"", headers=()):
            self.send_response(code)
            for name, value in headers:
                self.send_header(name, value)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)

        def releases(self):
            if not bundles:
                return fixed_releases
            return bundles.release_list("http://%s" % (self.headers.get("Host") or "%s:%d" % self.server.server_address))

        def download(self, name, query):
            if not bundles or "/" in name:
                self.send(404, b'{"message":"Not Found"}')
                return
            checksum = name.endswith(".sha256")
            real = name[:-7] if checksum else name
            path = os.path.join(bundles.folder, real)
            if not BUNDLE.match(real) or not os.path.exists(path):
                self.send(404, b'{"message":"Not Found"}')
                return
            if checksum:
                self.send(200, ("%s %s\n" % (bundles.sha256(real), real)).encode(),
                          [("Content-Type", "text/plain")])
                return
            size = os.stat(path).st_size
            start = 0
            rng = self.headers.get("Range", "")
            m = re.match(r"^bytes=(\d+)-$", rng)
            if m:
                start = int(m.group(1))
                if start >= size:
                    self.send(416, headers=[("Content-Range", "bytes */%d" % size)])
                    return
            rate = 0
            m = re.search(r"(?:^|&)rate=(\d+)", query)
            if m:
                rate = int(m.group(1)) * 1024
            self.send_response(206 if start else 200)
            self.send_header("Content-Type", "application/gzip")
            self.send_header("Content-Length", str(size - start))
            self.send_header("Accept-Ranges", "bytes")
            if start:
                self.send_header("Content-Range", "bytes %d-%d/%d" % (start, size - 1, size))
            self.end_headers()
            with open(path, "rb") as f:
                f.seek(start)
                while True:
                    chunk = f.read(8192 if rate else 1 << 16)
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        return
                    if rate:
                        time.sleep(len(chunk) / rate)

        def do_GET(self):
            log.append("%s if-none-match=%s user-agent=%s range=%s" % (
                self.path, self.headers.get("If-None-Match", ""), self.headers.get("User-Agent", ""),
                self.headers.get("Range", "")))
            path, _, query = self.path.partition("?")
            if path == "/releases":
                releases = self.releases()
                etag = 'W/"%s"' % hashlib.sha256(releases).hexdigest()
                if self.headers.get("If-None-Match") == etag:
                    self.send(304, headers=[("ETag", etag), ("X-RateLimit-Remaining", "59")])
                else:
                    self.send(200, releases, [("Content-Type", "application/json; charset=utf-8"), ("ETag", etag),
                                              ("X-RateLimit-Remaining", "59")])
            elif path.startswith("/download/"):
                self.download(path[len("/download/"):], query)
            elif path == "/ratelimit":
                self.send(403, rate_limit, [("Content-Type", "application/json; charset=utf-8"),
                                            ("X-RateLimit-Limit", "60"), ("X-RateLimit-Remaining", "0"),
                                            ("X-RateLimit-Reset", "1793480400")])
            elif path == "/error":
                self.send(500, b'{"message":"Server Error"}')
            elif path == "/notjson":
                self.send(200, b"<html>captive portal</html>", [("Content-Type", "text/html")])
            elif path == "/moved":
                self.send(302, headers=[("Location", "/releases")])
            elif path == "/log":
                self.send(200, ("\n".join(log) + "\n").encode())
            else:
                self.send(404, b'{"message":"Not Found"}')

    server = http.server.ThreadingHTTPServer((args.bind, args.port), Handler)
    print("port %d" % server.server_address[1], flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
