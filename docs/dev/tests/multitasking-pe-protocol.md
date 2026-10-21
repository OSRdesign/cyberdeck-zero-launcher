# Multitasking P-e - owner protocol (the real apps stay open in the background)

Phase P-e gives the native apps the app side of the lifecycle (APP-CONTRACT section 13, the toolkit's helper
`cp0_app_lifecycle_lvgl.h`): System Monitor, RSS Reader and the App Template declare `X-Background=freeze` (stopped
while hidden), Serial Console and Mesh Hop `run` (they keep reading their port or radio, draw nothing). Until now every
real app answered "cannot stay in the background". About 15 minutes on the deck; the Pi 3A+ DSI and the HackBerry are
optional (step 7). Nothing here is published: the packages are test builds (`~mt1`).

## 0. Setup (Controller, 3 min)

1. The launcher: the multitasking build already on the deck (`0.8.0-on.1`, multitasking on by default), staged
   (`python tools/boards/board.py test-start deck`) or installed. Check: `grep '\[mt\]' ~/applaunch-test/run.log` (staged)
   shows `[mt] multitasking P-b on`.
2. The apps: scratchpad `pkg-out\apps-mtpe\` (`INSTALL.txt`), one `dpkg -i` each (`inst_gen.py <host> <deb> <package>`):
   `system-monitor_0.2.4~mt1`, `serial-console_0.4.3~mt1`, `mesh-hop_0.5.1~mt1`, `rss-reader_0.1.1~mt1`, and optionally
   `cyberdeck-app-template_0.1.1~mt1`. `dpkg -s <package> | grep Version` shows the `~mt1` version.
3. Watch (staged launcher; for the installed one `journalctl --user -u cyberdeckzero -f` instead):

       t() { grep -E '\[lifecycle\]|\[mt\] (switcher|.*: (hide|show|ack|freeze|SIGSTOP|SIGCONT|close))' ~/applaunch-test/run.log | tail -n "${1:-10}"; }

## 1. System Monitor, freeze (2 min)

1. Open **Monitor**. Note the CPU tile. Press **Super** (or hold three fingers 1 s, then **Home**): the grid, a green dot on
   the Monitor tile. Log: `[cdk] system-monitor: can stay in the background (lifecycle on)` at the start, then
   `[lifecycle] hide 1: hidden (... ms)` and the launcher's freeze.
2. Wait 10 s. Hold three fingers on the grid (or **Alt+Tab**): the switcher shows the **Monitor** card, *Paused*. Tap it:
   Monitor is back **at once** as you left it, with no flash of the grid; its values move again within 2 s.
   Log: `[lifecycle] show 2: visible, full repaint`.
3. Long-press the **clock** (top right) 0.5 s in Monitor: the switcher opens (`[lifecycle] G2 (status strip held ... ms):
   switcher sent`). Tap the Monitor card to go back.
4. **Esc** at the overview still ends Monitor (the green dot goes). PASS when nothing stale happened (no page opened by
   itself, no quit) and the screen was whole each time.

## 2. Serial Console, run (3 min; a board on USB that prints, an ESP32 or Arduino; skip without one)

1. Open **Serial Console**, open the board's terminal, turn the **Log** on (key bar). Note the RX counter.
2. **Alt+Tab** (release Alt on Home, or press Super): the grid. Reset the board (its button) or let it print; wait 20 s.
3. Back from the switcher (the card says *Running*): the terminal shows what the board sent while hidden, the RX counter
   grew, and `tail ~/serial-logs/<the newest file>` has the same text. Type `abc` on the keyboard: the board receives
   `abc` (no stuck Alt: no Esc-prefixed bytes in the echo). PASS when no byte was lost and the keys are plain.

## 3. Mesh Hop, run (4 min; the MeshCore board on USB and a second node or phone that can send)

1. Open **Mesh Hop**, choose the node, open **Chats** (the list). Hide it with **Super**.
2. From the other node, send a direct message and a channel message to the deck. Wait 30 s.
3. `top -b -n 1 | grep -E 'mesh-hop|PID'` while hidden: a few % CPU at most (it only reads the radio).
4. Back from the switcher: the list shows both conversations with their **unread counts**, the messages are in them.
5. Open a conversation, hide, have the other node send one more, come back: the message is in the open conversation
   (read once it is shown). PASS when every message arrived and nothing was drawn over the grid while hidden.

## 4. RSS Reader, freeze (2 min; network)

1. Open **RSS Reader**, Favorites, **Refresh all**, and hide it at once (Super).
2. Wait 15 s, come back: the refresh goes on (or a feed says it timed out); Refresh all again works. PASS when nothing is
   broken and no tap or key of the grid reached the reader.

## 5. Close and the cap (2 min)

1. Open Monitor, Serial Console, Mesh Hop and RSS Reader one after the other, each hidden with Super: four dots.
2. Three fingers: the switcher, four cards. **Close all**: the cards and the dots go; each app's log line
   `[lifecycle] close N: quitting (hidden)`. Open Mesh Hop again: its history is there (it saved on close).
3. (Optional) Open the App Template too (a fifth app): the launcher closes the least recently used frozen app with a toast.

## 6. Without the launcher's socket (1 min)

Over ssh: `/usr/share/APPLaunch/bin/system-monitor --version` prints `0.2.4`; a start by hand on the console (or under the
installed 0.7 launcher) logs no `[lifecycle] on` line and behaves as before (Esc quits, three fingers under P-d offer
*Close Monitor?*).

## 7. Other boards (optional, 2 min each)

- **Pi 3A+ DSI** (`pi3a-dsi`, 800x480, FT5x06): steps 1 and 3 with three fingers and the clock long-press.
- **HackBerry** (720x720, its keyboard): step 1 with Alt+Tab, step 2's "no stuck Alt" with any serial board.

Results: one line per step (PASS / FAIL and what was seen), the `t 20` output of any FAIL.
