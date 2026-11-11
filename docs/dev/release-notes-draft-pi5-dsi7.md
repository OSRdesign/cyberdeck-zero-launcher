# Draft entry for the next release notes: board profile `pi5-dsi7`

**Board profile `pi5-dsi7`.** A Raspberry Pi 5 with the official Raspberry Pi 7-inch Touch Display on the RP1 DSI
port (800x480, ft5x06 touch, backlight regulator `10-0045`) is now detected by `install.sh` (before: `board: unknown`,
`--force` and a hand-written `board.conf` were needed). The profile is the one that was written by hand on the
owner's board: 800x480 landscape, no rotation, touch by capability in buffer orientation, the panel's backlight with
levels, idle dim 20 %, panel size 154x86 mm for the layout density. Touch and brightness were checked on that board
(2026-11-11). It is a desktop board when a display manager runs (lightdm + labwc on the tested board); `install.sh`
touches no boot configuration for it. An existing hand-written `board.conf` is kept as it is; `--force` replaces it
and keeps the old one as `board.conf.bak`. `--pwm-backlight` stays for the Pi 5 + HyperPixel 4 only. Force the
profile with `--board pi5-dsi7`.
