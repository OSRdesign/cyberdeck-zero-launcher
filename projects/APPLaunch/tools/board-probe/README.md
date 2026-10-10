# Board probe tools

Two small tools to find out how the launcher must be set up on a new board (Pi Zero 2 W, Pi 3A+,
Pi 5, CM5 boards, Walnut Pi, ...). Run them on each board and paste the results back.

| Tool | What it does | Changes anything? |
|---|---|---|
| `probe-board.sh` | Writes a report about the board: model, OS, glibc, memory, boot config, screens (framebuffers and DRM), backlight, touch and keyboards, Bluetooth, network, sound, temperature, and whether the launcher is installed. | **No.** Read-only, no sudo. It does not collect passwords, Wi-Fi keys or ssh keys; MAC addresses are masked. |
| `fb-test-pattern.py` | Draws a test picture on the screen to show rotation, mirroring, colour order and cropping. Optionally shows where touches land and works out the touch settings. | **Draws on the screen** while it runs, then puts the old picture back. Nothing is saved. |

## 1. Copy the tools to the board

From the PC, in this folder (replace `pi@board.local` with the board's user and address):

```
scp probe-board.sh fb-test-pattern.py pi@board.local:
```

## 2. Run the probe (on the board)

```
ssh pi@board.local
sh probe-board.sh
```

It takes a few seconds. It prints the report on the screen and also saves it as
`board-probe-<hostname>-<date>.md` in the current folder. The last block, `SUMMARY`, is the short
version. Messages such as "not installed" or "not readable" are normal.

Fetch the report back to the PC (one line, run on the PC):

```
scp 'pi@board.local:board-probe-*.md' .
```

Then paste the file's contents (or attach the file) in the chat.

## 3. Test pattern (optional, needs the screen)

Stop the launcher first if it is installed, because it owns the screen:

```
systemctl --user stop APPLaunch.service
python3 fb-test-pattern.py --rotate-hint          # shows the pattern on /dev/fb0 for 20 s
python3 fb-test-pattern.py --fb /dev/fb1          # another framebuffer (the probe lists them)
```

Take a photo of the screen and send it with the probe report. Ctrl-C stops early.

What you should see when everything is right: a big **F** in the top-left corner with
`TOP-LEFT` and the screen size under it, a green arrow pointing up (TOP), a cyan arrow pointing
right (RIGHT), coloured bars labelled R G B W K (red, green, blue, white, black), a smooth grey
gradient, a thin white line on every edge and a red frame 20 px inside. `--rotate-hint` prints how
to read a picture that is rotated, mirrored or cropped.

Touch (the board needs a touch screen):

```
python3 fb-test-pattern.py --touch auto           # 10 s: tap around, a square marks each raw touch
python3 fb-test-pattern.py --calibrate            # tap the 4 numbered crosses in order 1, 2, 3, 4
```

`--calibrate` prints lines like `APPLAUNCH_TOUCH_SWAP_XY=1`, `APPLAUNCH_TOUCH_INVERT_X=0`,
`APPLAUNCH_TOUCH_INVERT_Y=1` and a `LIBINPUT_CALIBRATION_MATRIX`: copy them into the chat.

Restart the launcher afterwards: `systemctl --user start APPLaunch.service`

## Boards with a desktop (Raspberry Pi OS desktop images)

On a desktop image a display manager (lightdm) starts a compositor (labwc, wayfire or Xorg). The
compositor owns the display, so a picture written to `/dev/fb0` may not show at all. The probe
reports this under "Desktop session" and on the `desktop:` SUMMARY line, and `fb-test-pattern.py`
prints a warning. Connect with ssh (stopping the desktop blanks the screen), then:

```
sudo systemctl stop lightdm                       # before the test
python3 fb-test-pattern.py --rotate-hint
sudo systemctl start lightdm                      # after the test
```

The launcher needs the same on such boards: it cannot draw while the desktop is running.

## If something goes wrong

- "no permission": add your user to the `video` and `input` groups
  (`sudo usermod -aG video,input $USER`, then log out and back in), or run with `sudo`.
- "cannot open /dev/fb0": the board has no framebuffer there; try the names the probe lists
  under "framebuffers". A board with DRM only and no `/dev/fb*` cannot run the pattern.
- The text console cursor may blink over the pattern; that is harmless.

For developers: `sh probe-board.sh --selftest` checks the input decoder;
`sh probe-board.sh --decode FILE` decodes a saved copy of `/proc/bus/input/devices`.
