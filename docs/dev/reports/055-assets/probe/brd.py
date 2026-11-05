#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Throwaway P0 helper: run a command on a board, or copy files, over ssh (paramiko).

The password comes only from the PIPW environment variable; it is never printed or written.
Every call opens one session and closes it before returning (an open session turns on the launcher's SSH glyph).

  python brd.py run  HOST "command"            [--timeout 60]
  python brd.py put  HOST LOCAL REMOTE          (files only; REMOTE directory must exist)
  python brd.py get  HOST REMOTE LOCAL
"""
import os
import sys
import time

import paramiko

USER = "osrde"


def connect(host, timeout):
    pw = os.environ.get("PIPW")
    if not pw:
        sys.exit("PIPW is not set")
    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(host, username=USER, password=pw, timeout=timeout, banner_timeout=timeout,
              auth_timeout=timeout, look_for_keys=False, allow_agent=False)
    return c


def main():
    a = sys.argv[1:]
    timeout = 60
    if "--timeout" in a:
        i = a.index("--timeout")
        timeout = int(a[i + 1])
        del a[i:i + 2]
    if len(a) < 3:
        sys.exit(__doc__)
    op, host = a[0], a[1]
    try:
        c = connect(host, 15)
    except paramiko.AuthenticationException:
        sys.exit("AUTH FAILED for %s (stopping)" % host)
    except Exception as e:  # noqa: BLE001
        sys.exit("CONNECT FAILED for %s: %s" % (host, e))
    rc = 0
    try:
        if op == "run":
            stdin, stdout, stderr = c.exec_command(a[2], timeout=timeout)
            stdin.close()
            out = stdout.read().decode("utf-8", "replace")
            err = stderr.read().decode("utf-8", "replace")
            rc = stdout.channel.recv_exit_status()
            sys.stdout.write(out)
            if err:
                sys.stderr.write(err)
        elif op == "putmany":            # putmany HOST REMOTE_DIR LOCAL...
            s = c.open_sftp()
            t0 = time.time()
            total = 0
            for f in a[3:]:
                s.put(f, a[2].rstrip("/") + "/" + os.path.basename(f))
                total += os.path.getsize(f)
            s.close()
            print("putmany %d files, %.1f MB in %.1f s" % (len(a) - 3, total / 1e6, time.time() - t0))
        elif op in ("put", "get"):
            s = c.open_sftp()
            t0 = time.time()
            if op == "put":
                s.put(a[2], a[3])
            else:
                s.get(a[2], a[3])
            s.close()
            print("%s %s -> %s in %.1f s" % (op, a[2], a[3], time.time() - t0))
        else:
            sys.exit(__doc__)
    finally:
        c.close()
    sys.exit(rc)


if __name__ == "__main__":
    main()
