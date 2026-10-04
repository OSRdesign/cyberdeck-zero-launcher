#!/usr/bin/env python3
"""Talk to the cyberdeck (Raspberry Pi Zero 2W) from the Windows dev machine. Used by the device operator.

    set PIPW=<the deck user's password>      (never write it into a file; the Controller provides it)
    python deck.py run "uname -a"            run a command as the deck user
    python deck.py sudo "systemctl restart x"  run a command with sudo (password piped, not shown)
    python deck.py put LOCAL REMOTE          upload a file (REMOTE is an absolute path or relative to ~)
    python deck.py get REMOTE LOCAL          download a file
    python deck.py shot out.png              screenshot of what is on the panel (reads /dev/fb0, 640x480)
    python deck.py install LOCAL REMOTE [mode]  upload LOCAL to a temp file and `sudo install` it to REMOTE (default 755)

Rules: use C:/ style paths for LOCAL on Windows; never `pkill -f` through here (it kills the ssh shell: use
`pkill -x name`); a long command should be started in the background and polled.
"""
import os
import shlex
import sys
import time

import paramiko

HOST = os.environ.get("DECK_HOST", "zero7.local")
USER = os.environ.get("DECK_USER", "osrde")


def connect():
    password = os.environ.get("PIPW")
    if not password:
        sys.exit("set the PIPW environment variable (the deck user's password)")
    for _ in range(20):                                   # mDNS answers late now and then
        try:
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(HOST, username=USER, password=password, timeout=15)
            return client
        except Exception:
            time.sleep(3)
    sys.exit("cannot reach " + HOST)


def run(client, command, sudo=False):
    if sudo:
        command = "sudo -S -p '' sh -c %s" % shlex.quote(command)      # the password goes in on stdin, never in the command line
    stdin, out, err = client.exec_command(command)
    if sudo:
        stdin.write(os.environ["PIPW"] + "\n")
        stdin.flush()
        stdin.channel.shutdown_write()
    return out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    action, args = sys.argv[1], sys.argv[2:]
    client = connect()
    if action == "run":
        sys.stdout.write(run(client, args[0]))
    elif action == "sudo":
        sys.stdout.write(run(client, args[0], sudo=True))
    elif action == "put":
        client.open_sftp().put(args[0], args[1])
        print("uploaded", args[0])
    elif action == "get":
        client.open_sftp().get(args[0], args[1])
        print("downloaded", args[1])
    elif action == "install":
        mode = args[2] if len(args) > 2 else "755"
        tmp = "/tmp/deck-install-%d" % os.getpid()
        client.open_sftp().put(args[0], tmp)
        sys.stdout.write(run(client, "install -m%s %s %s && rm -f %s && echo installed" % (mode, tmp, args[1], tmp), sudo=True))
    elif action == "shot":
        from PIL import Image
        _, out, _ = client.exec_command("head -c 1228800 /dev/fb0")
        Image.frombuffer("RGBA", (640, 480), out.read(), "raw", "BGRA", 0, 1).convert("RGB").save(args[0])
        print("saved", args[0])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
