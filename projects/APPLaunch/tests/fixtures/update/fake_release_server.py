#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# fake_release_server.py: a local stand-in for GitHub's release list, for Settings > Updates (report 047 P1). The
# launcher asks it instead of GitHub when APPLAUNCH_UPDATE_URL points here (PC tests: test_settings_update_fetch.cpp;
# a board: set APPLAUNCH_UPDATE_URL=http://<pc>:<port>/releases in the launcher's environment and restart it).
#
#   python3 fake_release_server.py [--port N] [--bind ADDR] [--file FILE]
#
#   GET /releases      FILE (default releases-0.8.13.json next to this script) with an ETag; 304 when If-None-Match
#                      matches; X-RateLimit-Remaining: 59
#   GET /ratelimit     403 with GitHub's rate limit body and headers (X-RateLimit-Remaining: 0, a reset time)
#   GET /error         500
#   GET /notjson       200 with a body that is no release list
#   GET /moved         302 to /releases (curl follows it)
#   GET /log           the If-None-Match and User-Agent of every request so far, one per line
#
# Prints "port N" once it listens (N: the port, an ephemeral one with --port 0). Ctrl+C or SIGTERM stops it.
import argparse
import hashlib
import http.server
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8047)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--file", default=os.path.join(HERE, "releases-0.8.13.json"))
    args = parser.parse_args()
    with open(args.file, "rb") as f:
        releases = f.read()
    with open(os.path.join(HERE, "rate-limit.json"), "rb") as f:
        rate_limit = f.read()
    etag = 'W/"%s"' % hashlib.sha256(releases).hexdigest()
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

        def do_GET(self):
            log.append("%s if-none-match=%s user-agent=%s" % (
                self.path, self.headers.get("If-None-Match", ""), self.headers.get("User-Agent", "")))
            path = self.path.split("?", 1)[0]
            if path == "/releases":
                if self.headers.get("If-None-Match") == etag:
                    self.send(304, headers=[("ETag", etag), ("X-RateLimit-Remaining", "59")])
                else:
                    self.send(200, releases, [("Content-Type", "application/json; charset=utf-8"), ("ETag", etag),
                                              ("X-RateLimit-Remaining", "59")])
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
