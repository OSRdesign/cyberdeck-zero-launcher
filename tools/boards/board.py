#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deploy and test the launcher on the boards of boards.json over ssh (paramiko), from the Windows dev machine.

    python board.py [--dry-run] upload     BOARD BUNDLE   upload the bundle tarball of the board's arch (cyberdeckzero-
                                                          launcher-<version>-<arch>.tar.gz; arm64: or its copy
                                                          pizero2w-bundle.tar.gz), unpack it, stage the test launcher
    python board.py [--dry-run] install    BOARD [--check] [--install-arg ARG ...] [--force]
                                                          run the bundle's install.sh (sudo) for the board's user
                                                          (--force is passed to install.sh: unknown board, edited profile)
    python board.py [--dry-run] test-start BOARD [--env K=V ...]
                                                          run the staged test launcher (desktop boards: stop the
                                                          display manager first); started again when it exits with
                                                          status 76 (Settings > Screen > PDA mode, as its units do)
    python board.py [--dry-run] test-stop  BOARD          stop it, restore the desktop / the installed launcher, remove test files
    python board.py [--dry-run] grab       BOARD OUT.png  framebuffer screenshot, turned the way the user sees the panel
    python board.py [--dry-run] status     BOARD          launcher, services / desktop, text console, test files, board profile

Password: the PW environment variable (PIPW, as docs/dev/deck/deck.py uses, is accepted too). It is never written to
a file, never put on a command line (sudo reads it on stdin: sudo -S) and never printed. --dry-run needs no password
and no network: it prints every command it would run.

Board arch (boards.json "arch", default arm64): the Pi boards are arm64, a PC (the FZ-M1 tablet) amd64. upload refuses a
bundle of the other arch: by its name, by its launcher's ELF header when the tarball is here, and on the board against
`uname -m` once unpacked.

Board kinds (boards.json):
  service  the launcher runs as the user service cyberdeckzero.service (the deck, the Pi 3A+, the FZ-M1 tablet: it
           boots to the text console, no display manager). test-start stops the
           service (and APPLaunch.service, its name up to v0.6.0, on a board not migrated yet) and runs the staged
           launcher from ~/applaunch-test (cwd there; the installed /usr/share/APPLaunch stays as it is); test-stop kills
           it, removes the test files and restarts the installed service detached (cyberdeckzero.service, else
           APPLaunch.service; setsid nohup systemctl --user restart ... &), so the ssh call returns at once.
  desktop  a display manager owns the screen (Raspberry Pi OS desktop, lightdm + labwc: Pi 5, HackBerry; or a PC with a
           desktop, e.g. the FZ-M1 with Armbian's XFCE: lightdm + Xorg). install.sh installs the desktop variant there
           (cyberdeckzero-desktop.service, started from the applications menu, gives the desktop back on exit).
           test-start stops that unit (and applaunch-desktop.service, its name up to v0.6.0) if it runs, stops the
           display manager, links /usr/share/APPLaunch -> ~/applaunch-test (only when nothing real is there) and runs
           the staged launcher; test-stop removes the link (only ours), starts the display manager again and removes the
           test files. The display manager is boards.json "dm" (its unit name without .service; default lightdm).
  A board installed as a service board that gets a desktop later: run install.sh again (it switches to the desktop
  variant), then set its kind to "desktop" (and "dm" when it is not lightdm) in boards.json.

Quirks encoded here:
  - Pi 3A+ and Pi 4 B (quirk no_environ): an ssh command string containing the word "environ" drops the connection. board.py
    refuses to send one to such a board (dry run included). Note EnvironmentFile= in a unit file and /proc/PID/environ.
  - never `pkill -f` over ssh (it kills the ssh shell itself): `pkill -x cyberdeckzero` (the comm name: 13 characters,
    the whole binary name), and the earlier binary's 15-character comm name (OLD_LAUNCHER_COMM) for a board still on
    v0.6.0 or a launcher started through the earlier name's link.
  - a backgrounded remote process has stdin, stdout and stderr redirected, else the ssh call never returns.
  - `~` inside `sudo sh -c` is root's home: absolute /home/<user>/... paths are used.

Windows: use C:/ style paths for local files. `python -I board.py` works for -h and --dry-run; a real run needs
paramiko (and Pillow for grab) importable, so drop -I if they are installed in the user site-packages.
"""
import argparse
import json
import os
import shlex
import sys
import tarfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = "cyberdeckzero"                       # /usr/share/APPLaunch/bin/cyberdeckzero, bundle payload/bin/cyberdeckzero
LAUNCHER_COMM = BINARY                         # its /proc comm: the whole name (13 characters, under the 15 limit)
REMOTE_TAR = "/tmp/applaunch-upload.tar.gz"
REMOTE_BUNDLE = "/tmp/applaunch-bundle"       # the tarball unpacks to REMOTE_BUNDLE/bundle/...
SERVICE = "cyberdeckzero.service"              # service boards: the launcher as a user service
DESKTOP_UNIT = "cyberdeckzero-desktop.service" # desktop boards: the launcher started from the applications menu
CONSOLE_UNIT = "cyberdeckzero-console.service" # service boards: the panel leaves the text console after boot
# The names up to v0.6.0 (install.sh migrates them; a board not migrated yet still has them)
# branding-allow-begin: the comm name of the launcher binary up to v0.6.0 (15 characters of its name)
OLD_LAUNCHER_COMM = "M5CardputerZero"
# branding-allow-end
OLD_SERVICE = "APPLaunch.service"
OLD_DESKTOP_UNIT = "applaunch-desktop.service"
OLD_CONSOLE_UNIT = "applaunch-console.service"
ARCHES = {"arm64": ("aarch64", 183), "amd64": ("x86_64", 62)}   # arch: uname -m, ELF e_machine
LEGACY_BUNDLE = "pizero2w-bundle.tar.gz"                          # the arm64 bundle's copy under its earlier name


class BoardError(Exception):
    pass


def load_boards(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)["boards"]


class Board:
    def __init__(self, name, spec):
        self.name = name
        self.host = spec["host"]
        self.user = spec["user"]
        self.kind = spec["kind"]
        self.fb = spec["fb"]
        self.rotate = int(spec.get("rotate", 0))
        self.test_env = dict(spec.get("test_env", {}))
        self.quirks = set(spec.get("quirks", []))
        self.arch = spec.get("arch", "arm64")
        self.dm = spec.get("dm", "lightdm")              # desktop boards: the display manager's unit, without .service
        if self.kind not in ("service", "desktop"):
            raise BoardError("%s: unknown kind %r (service|desktop)" % (name, self.kind))
        if not self.dm or not all(c.isalnum() or c in "-_.@" for c in self.dm) or self.dm.endswith(".service"):
            raise BoardError("%s: unusable dm %r (a unit name without .service, e.g. lightdm, gdm3, sddm)"
                             % (name, self.dm))
        if self.arch not in ARCHES:
            raise BoardError("%s: unknown arch %r (%s)" % (name, self.arch, "|".join(sorted(ARCHES))))

    @property
    def home(self):
        return "/home/%s" % self.user

    @property
    def test_dir(self):
        return self.home + "/applaunch-test"


class Remote:
    """One ssh connection to a board, or a printer of the commands in a dry run."""

    def __init__(self, board, dry_run):
        self.board = board
        self.dry_run = dry_run
        self.client = None
        self.password = None

    # ------------------------------------------------------------------ connection
    def _connect(self):
        if self.client:
            return
        self.password = os.environ.get("PW") or os.environ.get("PIPW")
        if not self.password:
            raise BoardError("set the PW environment variable (the board user's password); it is never stored")
        import paramiko                                          # lazy: -h and --dry-run need no paramiko
        last = None
        for _ in range(10):                                      # mDNS (zero7.local) answers late now and then
            try:
                client = paramiko.SSHClient()
                client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
                client.connect(self.board.host, username=self.board.user, password=self.password, timeout=20)
                self.client = client
                return
            except Exception as error:                           # noqa: BLE001 (any failure: retry, then report)
                last = error
                time.sleep(3)
        raise BoardError("cannot reach %s (%s): %s" % (self.board.name, self.board.host, last))

    def close(self):
        if self.client:
            self.client.close()

    # ------------------------------------------------------------------ commands
    def _check(self, command):
        if "no_environ" in self.board.quirks and "environ" in command.lower():
            raise BoardError("%s drops the ssh connection on a command containing 'environ': not sent:\n  %s"
                             % (self.board.name, command))

    def run(self, command, sudo=False, timeout=300, check=False):
        """Runs command (as the user, or as root through sudo -S with the password on stdin). Prints its output,
        returns its exit status."""
        self._check(command)
        full = "sudo -S -p '' sh -c %s" % shlex.quote(command) if sudo else command
        if self.dry_run:
            print("%s$ %s" % ("[sudo] " if sudo else "", command))
            return 0
        self._connect()
        stdin, out, err = self.client.exec_command(full, timeout=timeout)
        if sudo:
            stdin.write(self.password + "\n")
            stdin.flush()
        stdin.channel.shutdown_write()
        text = out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")
        status = out.channel.recv_exit_status()
        if text:
            sys.stdout.write(text if text.endswith("\n") else text + "\n")
        if check and status != 0:
            raise BoardError("exit %d: %s" % (status, command))
        return status

    def read_bytes(self, command, timeout=60):
        self._check(command)
        if self.dry_run:
            print("$ %s   (output read as bytes)" % command)
            return b""
        self._connect()
        _, out, err = self.client.exec_command(command, timeout=timeout)
        data = out.read()
        problem = err.read().decode("utf-8", "replace").strip()
        if problem:
            print(problem)
        return data

    def put(self, local, remote):
        if self.dry_run:
            print("sftp put %s -> %s:%s" % (local, self.board.host, remote))
            return
        self._connect()
        sftp = self.client.open_sftp()
        try:
            sftp.put(local, remote)
        finally:
            sftp.close()
        print("uploaded %s -> %s (%d bytes)" % (local, remote, os.path.getsize(local)))


# ---------------------------------------------------------------------------------------------------------- actions
def bundle_name_arch(path):
    """The arch a bundle's file name says: cyberdeckzero-launcher-<version>-<arch>.tar.gz (pizero2w-bundle.tar.gz:
    arm64), None for another name."""
    name = os.path.basename(path)
    if name == LEGACY_BUNDLE:
        return "arm64"
    for arch in ARCHES:
        if name.startswith("cyberdeckzero-launcher-") and name.endswith("-%s.tar.gz" % arch):
            return arch
    return None


def bundle_elf_arch(path):
    """The arch of the launcher binary inside a bundle tarball (its ELF e_machine), None when unreadable."""
    try:
        with tarfile.open(path, "r:gz") as tar:
            member = tar.extractfile("bundle/payload/bin/" + BINARY)
            head = member.read(20) if member else b""
    except (OSError, KeyError, tarfile.TarError):
        return None
    if len(head) < 20 or head[:4] != b"\x7fELF":
        return None
    machine = head[18] | (head[19] << 8)
    return next((arch for arch, (_, m) in ARCHES.items() if m == machine), None)


def check_bundle_arch(board, bundle):
    named = bundle_name_arch(bundle)
    if named and named != board.arch:
        raise BoardError("%s is an %s bundle, %s is %s: use cyberdeckzero-launcher-<version>-%s.tar.gz"
                         % (os.path.basename(bundle), named, board.name, board.arch, board.arch))
    if os.path.isfile(bundle):
        inside = bundle_elf_arch(bundle)
        if inside and inside != board.arch:
            raise BoardError("%s holds an %s launcher, %s is %s" % (os.path.basename(bundle), inside, board.name,
                                                                    board.arch))


def do_upload(remote, board, args):
    bundle = args.bundle
    if not os.path.isfile(bundle):
        if not remote.dry_run:
            raise BoardError("no such bundle: %s" % bundle)
        print("(dry run: %s does not exist here)" % bundle)
    check_bundle_arch(board, bundle)
    remote.put(bundle, REMOTE_TAR)
    t = board.test_dir
    payload = REMOTE_BUNDLE + "/bundle/payload"
    machine, e_machine = ARCHES[board.arch]
    # unpack for install.sh, check that the payload's launcher is the board's arch (ELF e_machine against uname -m),
    # and stage the test launcher (bin, lib, share) in ~/applaunch-test for test-start
    return remote.run(
        "rm -rf {b} {t} && mkdir -p {b} {t}/bin {t}/lib && tar xzf {tar} -C {b} && "
        "m=$(od -An -tu1 -j18 -N2 {p}/bin/{bin} | tr -s ' ') && "
        "if [ \"$(uname -m)/$m\" = '{machine}/ {e_machine} 0' ]; then echo payload: {arch} launcher, $(uname -m) board; "
        "else echo \"payload launcher (e_machine$m) is not for this $(uname -m) board\"; exit 1; fi && "
        "cp -a {p}/share/. {t}/ && cp {p}/bin/{bin} {t}/bin/ && cp {p}/lib/libapplaunch_vfb.so {t}/lib/ && "
        "ls {b}/bundle && echo staged: {t}".format(b=REMOTE_BUNDLE, t=t, tar=REMOTE_TAR, p=payload, bin=BINARY,
                                                   machine=machine, e_machine=e_machine, arch=board.arch),
        check=True)


def install_options(board, args):
    """install.sh's options: the board's user, --dry-run for --check, --force passed on, then --install-arg ones."""
    options = ["--user", board.user] + (["--dry-run"] if args.check else []) + (["--force"] if args.force else [])
    return options + [x for x in (args.install_arg or []) if not (x == "--force" and args.force)]


def do_install(remote, board, args):
    # Desktop boards are supported: install.sh sees their display manager and installs the desktop variant
    # (cyberdeckzero-desktop.service + menu entry; nothing starts now, nothing at boot). Over v0.6.0 or before it
    # migrates the earlier unit names first (their units stopped, disabled and removed).
    options = install_options(board, args)
    log = "/tmp/applaunch-install.log"
    # service boards: install.sh restarts the user service itself (runuser ... systemctl --user restart
    # cyberdeckzero.service). --check shows the whole plan (the migration steps, the desktop unit text), an install
    # its last lines.
    lines = 400 if args.check else 60
    return remote.run("cd {b}/bundle && sh ./install.sh {o} > {log} 2>&1; rc=$?; tail -n {n} {log}; exit $rc".format(
        b=REMOTE_BUNDLE, o=" ".join(shlex.quote(x) for x in options), log=log, n=lines), sudo=True, timeout=900)


def launch_command(board, env):
    assignments = " ".join("%s=%s" % (key, shlex.quote(value)) for key, value in env.items())
    # detached (setsid nohup, all three streams redirected) so the ssh call returns; line-buffered log in run.log; run
    # again while it exits with status 76 (a PDA mode change asks for a restart, as the units' RestartForceExitStatus=)
    return ("cd {t} && test -x bin/{bin} || {{ echo 'no staged launcher in {t}: run upload first'; exit 1; }}; "
            "(setsid nohup env {env} sh -c 'while stdbuf -oL -eL ./bin/{bin}; [ $? = 76 ]; do :; done' "
            "> run.log 2>&1 < /dev/null &) ; sleep 8; "
            "pgrep -x {comm} | head -2; grep -E 'display-profile|rror|fail' run.log | head -8; tail -n 3 run.log"
            ).format(t=board.test_dir, bin=BINARY, env=assignments, comm=LAUNCHER_COMM)


def parse_env(pairs):
    env = {}
    for pair in pairs or []:
        if "=" not in pair or pair.startswith("="):
            raise BoardError("--env takes KEY=VALUE, not %r" % pair)
        key, value = pair.split("=", 1)
        env[key] = value
    return env


def do_test_start(remote, board, args):
    env = dict(board.test_env)
    env.update(parse_env(args.env))
    launch = launch_command(board, env)
    remote._check(launch)                                        # refuse before anything on the board is stopped
    if board.kind == "service":
        # the installed service by its name, and by its name up to v0.6.0 when the board still has that unit
        remote.run("systemctl --user stop {svc}; "
                   "if systemctl --user cat {old} > /dev/null 2>&1; then systemctl --user stop {old}; fi; "
                   "pkill -x {comm}; pkill -x {old_comm}; sleep 1; "
                   "systemctl --user is-active {svc} {old} || true".format(
                       svc=SERVICE, old=OLD_SERVICE, comm=LAUNCHER_COMM, old_comm=OLD_LAUNCHER_COMM))
    else:
        # an installed launcher started from the menu goes first (its stop brings the display manager back, stopped
        # just after)
        remote.run(
            "systemctl stop {unit} 2>/dev/null; systemctl stop {old_unit} 2>/dev/null; "
            "pkill -x {comm}; pkill -x {old_comm}; systemctl stop {dm}; sleep 3; systemctl is-active {dm}; "
            "if [ -L /usr/share/APPLaunch ] || [ ! -e /usr/share/APPLaunch ]; then ln -sfn {t} /usr/share/APPLaunch; "
            "else echo '/usr/share/APPLaunch is a real directory: left alone'; fi; ls -ld /usr/share/APPLaunch".format(
                unit=DESKTOP_UNIT, old_unit=OLD_DESKTOP_UNIT, comm=LAUNCHER_COMM, old_comm=OLD_LAUNCHER_COMM,
                dm=board.dm, t=board.test_dir), sudo=True)
    return remote.run(launch)


def do_test_stop(remote, board, args):
    cleanup = "rm -rf {t} {b} {tar}; ls -d {t} 2>/dev/null || echo test files removed".format(
        t=board.test_dir, b=REMOTE_BUNDLE, tar=REMOTE_TAR)
    if board.kind == "service":
        remote.run("pkill -x %s; sleep 1; %s" % (LAUNCHER_COMM, cleanup))
        # detached restart (Pi 3A+: a plain restart over ssh can hang the session) of the installed service: its
        # name, or its name up to v0.6.0 on a board not migrated yet
        remote.run("u={svc}; systemctl --user cat {svc} > /dev/null 2>&1 || u={old}; "
                   "setsid nohup systemctl --user restart $u > /dev/null 2>&1 < /dev/null &".format(
                       svc=SERVICE, old=OLD_SERVICE))
        return remote.run("sleep 4; systemctl --user is-active {svc} {old}; pgrep -x {comm} || pgrep -x {old_comm} "
                          "|| echo launcher-not-running".format(svc=SERVICE, old=OLD_SERVICE, comm=LAUNCHER_COMM,
                                                                old_comm=OLD_LAUNCHER_COMM))
    status = remote.run(
        "pkill -x {comm}; sleep 1; if [ -L /usr/share/APPLaunch ] && [ \"$(readlink /usr/share/APPLaunch)\" = {t} ]; "
        "then rm /usr/share/APPLaunch && echo link removed; fi; systemctl start {dm}; sleep 2; "
        "systemctl is-active {dm}".format(comm=LAUNCHER_COMM, t=board.test_dir, dm=board.dm), sudo=True)
    remote.run(cleanup)
    return status


def do_grab(remote, board, args):
    fb = board.fb
    width, height, bpp, fmt = int(fb["width"]), int(fb["height"]), int(fb["bpp"]), fb["format"]
    size = width * height * bpp // 8
    data = remote.read_bytes("head -c %d %s" % (size, fb["device"]))
    if remote.dry_run:
        print("decode %dx%d %s, rotate %d deg counter-clockwise, save %s" % (width, height, fmt, board.rotate, args.out))
        return 0
    if len(data) < size:
        raise BoardError("read %d bytes of %s, expected %d" % (len(data), fb["device"], size))
    from PIL import Image                                       # lazy: only grab needs Pillow
    if fmt == "BGRA":
        image = Image.frombuffer("RGBA", (width, height), data[:size], "raw", "BGRA", 0, 1).convert("RGB")
    elif fmt == "RGB565":
        image = Image.new("RGB", (width, height))
        pixels = []
        for i in range(0, size, 2):
            p = data[i] | (data[i + 1] << 8)                     # little endian
            pixels.append((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
        image.putdata(pixels)
    else:
        raise BoardError("unknown framebuffer format %r" % fmt)
    if args.raw:
        base, ext = os.path.splitext(args.out)
        image.save(base + "-raw" + (ext or ".png"))
    if board.rotate:
        image = image.rotate(board.rotate, expand=True)          # counter-clockwise: what the user sees
    image.save(args.out)
    print("saved %s (%dx%d)" % (args.out, image.width, image.height))
    return 0


def do_status(remote, board, args):
    t = board.test_dir
    # the launcher by its name; a launcher by the earlier binary name (v0.6.0, or started through the earlier name's
    # link) is reported apart. Apps still named with the earlier prefix share that comm name: only the command lines
    # with "-APPLaunch" (the launcher's earlier name) count.
    common = ("uname -n; uptime; pgrep -ax {comm} || echo launcher-not-running; "
              "pgrep -ax {old_comm} | grep -e -APPLaunch && echo earlier-binary-name-running; "
              "ls -ld /usr/share/APPLaunch {t} 2>&1; "
              "cat /etc/applaunch/board.conf 2>/dev/null | grep -v '^#' | head -20").format(
                  comm=LAUNCHER_COMM, old_comm=OLD_LAUNCHER_COMM, t=t)
    # the framebuffer console: bound = the kernel console may repaint over the launcher (no "environ" in here: Pi 3A+)
    console = ("for d in /sys/class/vtconsole/vtcon*; do grep -q 'frame buffer' $d/name 2>/dev/null && "
               "echo text-console: $(basename $d) bind=$(cat $d/bind); done")
    if board.kind == "service":
        extra = ("echo service: $(systemctl --user is-active {svc}); echo console-unit: $(systemctl is-active {con}); "
                 "echo old-service {old_svc}: $(systemctl --user is-active {old_svc}); "
                 "echo old-console-unit {old_con}: $(systemctl is-active {old_con}); {console}").format(
                     svc=SERVICE, con=CONSOLE_UNIT, old_svc=OLD_SERVICE, old_con=OLD_CONSOLE_UNIT, console=console)
    else:
        extra = ("echo {dm}: $(systemctl is-active {dm}); echo {unit}: $(systemctl is-active {unit}); "
                 "echo old {old_unit}: $(systemctl is-active {old_unit}); "
                 "pgrep -x labwc > /dev/null && echo labwc-running || echo labwc-not-running; "
                 "pgrep -x Xorg > /dev/null && echo Xorg-running || echo Xorg-not-running; {console}").format(
                     dm=board.dm, unit=DESKTOP_UNIT, old_unit=OLD_DESKTOP_UNIT, console=console)
    return remote.run(common + "; " + extra)


ACTIONS = {"upload": do_upload, "install": do_install, "test-start": do_test_start, "test-stop": do_test_stop,
           "grab": do_grab, "status": do_status}


def build_parser(board_names):
    parser = argparse.ArgumentParser(prog="board.py", description=__doc__.split("\n")[0],
                                     epilog="Boards: %s. See the module docstring (or README.md) for kinds and quirks."
                                     % ", ".join(board_names),
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="print the commands, connect to nothing")
    parser.add_argument("--boards", default=os.path.join(HERE, "boards.json"), help="board table (default: boards.json here)")
    sub = parser.add_subparsers(dest="action", metavar="ACTION")
    sub.required = True

    def board_arg(p):
        p.add_argument("board", choices=board_names, metavar="BOARD", help="one of: " + ", ".join(board_names))

    p = sub.add_parser("upload", help="upload and unpack a bundle, stage the test launcher in ~/applaunch-test")
    board_arg(p)
    p.add_argument("bundle", help="local bundle tarball of the board's arch, cyberdeckzero-launcher-<version>-<arch>"
                   ".tar.gz (arm64: or pizero2w-bundle.tar.gz; C:/ style path on Windows)")
    p = sub.add_parser("install", help="run the uploaded bundle's install.sh with sudo")
    board_arg(p)
    p.add_argument("--check", action="store_true", help="install.sh --dry-run: print what it would do")
    p.add_argument("--install-arg", action="append", metavar="ARG", help="extra install.sh option (repeatable)")
    p.add_argument("--force", action="store_true", help="install.sh --force (unknown board without a profile, "
                   "replace a hand-edited board.conf); desktop boards need no --force")
    p = sub.add_parser("test-start", help="run the staged test launcher (desktop boards: stop the display manager first)")
    board_arg(p)
    p.add_argument("--env", action="append", metavar="K=V", help="extra launcher environment (repeatable), e.g. "
                   "APPLAUNCH_DEV=1 APPLAUNCH_OSK=force")
    p = sub.add_parser("test-stop", help="stop the test launcher, restore desktop / service, remove test files")
    board_arg(p)
    p = sub.add_parser("grab", help="framebuffer screenshot as the user sees it")
    board_arg(p)
    p.add_argument("out", help="output PNG")
    p.add_argument("--raw", action="store_true", help="also save the unrotated buffer as <out>-raw.png")
    p = sub.add_parser("status", help="launcher, service or desktop, test files, board profile")
    board_arg(p)
    return parser


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except AttributeError:
            pass
    argv = sys.argv[1:] if argv is None else argv
    boards_path = os.path.join(HERE, "boards.json")
    if "--boards" in argv[:-1]:
        boards_path = argv[argv.index("--boards") + 1]
    boards = load_boards(boards_path)
    args = build_parser(sorted(boards)).parse_args(argv)
    remote = None
    try:
        board = Board(args.board, boards[args.board])
        remote = Remote(board, args.dry_run)
        if args.dry_run:
            print("# dry run: %s (%s@%s, %s board, %s)" % (board.name, board.user, board.host, board.kind, board.arch))
        return ACTIONS[args.action](remote, board, args) or 0
    except BoardError as error:
        print("board.py: %s" % error, file=sys.stderr)
        return 1
    finally:
        if remote:
            remote.close()


if __name__ == "__main__":
    sys.exit(main())
