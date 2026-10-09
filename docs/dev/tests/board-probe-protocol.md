# Board probe protocol - Mesh Hop phase 3 board checks (about 30 minutes)

Goal: answer the four open questions of `tasks/011-mesh-hop-roadmap.md` ("Needs a board test before coding") with the real board
(Seeed XIAO nRF52840, companion firmware v1.15.0, USB serial) before phase 3 is coded. The probe is `apps/mesh-hop/tools/board_probe.py`
(Python 3 standard library only; it prints every frame sent and received as hex with the decoded name, and PASS / NOTE lines).

What it changes on the board: only one test text on a channel (it goes on the air) and the flood scope (always restored to null).
It never sends FACTORY_RESET and never reads the message queue (no SYNC_NEXT), so the app's waiting messages stay on the board.

## 0. Prepare

1. Quit Mesh Hop on the deck (hold Esc 3 s). The board must stay plugged in on `/dev/ttyACM0`.
2. Copy the script to the deck (from the PC, PowerShell or WSL; use the deck's user and address you already use, enter the password
   when asked, never put it in a file):

       scp C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\mesh-hop\tools\board_probe.py <user>@<deck>:/tmp/board_probe.py

3. On the deck, in a terminal (SSH): `python3 /tmp/board_probe.py stats`. If it says `ERROR: ... is in use by pid ...`, that process
   (usually Mesh Hop) is still running: stop it and retry. If it says permission denied: `sudo usermod -aG dialout $USER`, log in again
   (or run the commands with `sudo`).

Save the whole output of each step: run it as `python3 /tmp/board_probe.py <command> 2>&1 | tee /tmp/probe-<name>.txt`.

## 1. stats (1 minute)

    python3 /tmp/board_probe.py stats 2>&1 | tee /tmp/probe-stats.txt

Expected: three `TX GET_STATS` / `RX STATS` pairs and `PASS: stats sub type N: ...` lines (core: battery, uptime; radio: noise floor,
SNR, airtime; packets: counters). `NOTE: ... error 1 (UNSUPPORTED_CMD)` means v1.15.0 lacks that sub type: that is a valid answer.

## 2. factory-reset-support (1 minute)

    python3 /tmp/board_probe.py factory-reset-support 2>&1 | tee /tmp/probe-info.txt

Expected: a `PASS: device info: fw_level 11, ... version 'v1.15.0-...'` line and a NOTE that the real reset test is separate. Nothing is
sent that changes the board. (The real reset test, `33 "reset"` versus a bare `33`, wipes the board's identity: do it only on a board
you accept to reset, as a separate step the controller will define.)

## 3. flood-scope (2 minutes)

    python3 /tmp/board_probe.py flood-scope 2>&1 | tee /tmp/probe-scope.txt

Sends SET_FLOOD_SCOPE (command 54) with a null key, then with the key of `#test` (first 16 bytes of SHA-256 of the name), then null
again, always. Expected: three `RESP_OK` (`PASS`) lines and no NOTE about the restore. `UNSUPPORTED_CMD` or `ILLEGAL_ARG` is a valid
answer; paste it. If the last line says the restore was not confirmed, unplug and replug the board and tell the controller.
Another region name: `flood-scope --region '#yourregion'`.

## 4. channel-send (2 minutes) - transmits on the air

    python3 /tmp/board_probe.py channel-send 2>&1 | tee /tmp/probe-send.txt

It shows the channel (index 0, Public) and the text `mesh-hop board probe HH:MM:SS (test, please ignore)`, and asks `y`. Everybody on
the public channel in range can read it: say so before you run it, or use a private channel with `--index N`.
Expected: `RX RESP_OK` (`00`) or `RX MSG_SENT` (`06 ...`) right after the `TX`, a `PASS` line naming which, then 20 s of pushes.
This is the answer to "which frame does the board return for a channel send".

## 5. log-echo with our own message (3 minutes, the main test)

    python3 /tmp/board_probe.py log-echo --send --seconds 120 2>&1 | tee /tmp/probe-echo.txt

Sends one test text (asks `y`), then listens 120 s and prints every `PUSH_LOG_RX_DATA` (0x88) with the decoded packet type, hops and,
for channel texts, the channel hash. At the end it lists the channel packets heard after the send, grouped by identical payload, with
the delay and hop count of each copy. Our own message came back as an echo when a payload shows up 1 s to a few seconds after the
send with hops >= 1. The probe cannot decrypt, so on a busy channel other people's texts look the same: run it at a quiet moment,
or on a private channel (`--index N`) with a repeater in range. Hear nothing: say whether a repeater is in range
(the app normally shows "heard back" only if one is).
Listen only, no send: `python3 /tmp/board_probe.py log-echo --seconds 60`.

## What to paste back

The five files `/tmp/probe-*.txt` (or at least the `RX` lines of steps 4 and 5 and every `PASS` / `NOTE` line), plus: was a repeater
in range during step 5 (yes / no / unknown), and did the text show up on another device? Then start Mesh Hop again.

## Not covered (separate steps)

The real FACTORY_RESET test and whether SET_FLOOD_SCOPE changes the over-the-air behaviour (needs a second radio or a repeater log).
Unsure protocol constants: see the hand-off report (SET_FLOOD_SCOPE command code 54 and its frame `36 00 <16 key bytes>`; the region
key derivation SHA-256(name)[:16]).
