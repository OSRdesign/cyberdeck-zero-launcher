**New since v0.7.0** (pull requests #45 to #67; multitasking is #61 to #67)

- **Multitasking: a touch-first app switcher for low-power boards.** Up to 4 apps stay open. Switching back to one
  puts its last picture back at once (no flash) and it carries on where it was.
  - **Open apps:** hold three fingers still on the screen for 1 s, anywhere; or long-press the clock / status strip
    for 0.5 s; or press Alt+Tab on a keyboard (release Alt to switch). Super goes to the home grid. The switcher
    shows a card per open app, **Home** and **Close all**.
  - **Apps choose** how they live in the background with one line in their `.desktop` file: `X-Background=run`
    (keeps working while hidden, for example logging a serial port) or `X-Background=freeze` (stopped while hidden,
    no CPU). The app contract's section 13 is final (`docs/APP-CONTRACT.md`), and the toolkit helper
    `cp0_app_lifecycle` in `cp0_lvgl` does the work for apps built on it.
  - **Apps that declare nothing** are not kept: they are paused behind a "Close <app>?" sheet (Close / Keep).
  - A **green dot** on a home tile marks an app open in the background.
  - **Low-power rules:** at most 4 resident apps (a 5th launch closes the least recently used frozen one); when free
    memory drops under 64 MB the launcher closes a frozen app, under 32 MB a running one too; hidden apps get a higher
    `oom_score_adj`, so the kernel kills them first under memory pressure.
  - **On by default** on screens of 640x480 and more (the deck, the Pi 3A+ DSI 800x480, the HackBerry CM5 720x720,
    the Pi 5 HyperPixel); **off** on smaller ones (480x320, 320x170), where the launcher behaves as before.
- **Apps updated for it** (published separately in the apps repository, update them in Settings > Apps):
  System Monitor 0.2.4 and RSS Reader 0.1.1 (`freeze`), Serial Console 0.4.3 and Mesh Hop 0.5.1 (`run`).
- Also since v0.7.0: `cp0::ui::CompactList` for apps (#45), home tiles reload when an icon changes (#46), the status
  line uses the full width (#52), row affordances in lists (#54 to #56), two standard touch sizes, 5 mm and 7 mm
  (#57), the Calculator fills the screen (#58), pointer navigation with a trackpoint or mouse (#59, off by default),
  the native page header's Esc stays 9 mm (#60).

**How to use**

- Open two apps that support it (for example Mesh Hop and System Monitor), then hold three fingers on the screen for
  1 s, or long-press the clock, or press Alt+Tab: "Open apps" shows both. Tap a card to go back to it, tap its X to
  close it, or Close all.
- **Turn it off:** set `APPLAUNCH_MULTITASK=0` in the launcher's environment (`=1` forces it on, also on a small
  screen). For the user service, as the deck user:

```
mkdir -p ~/.config/systemd/user/cyberdeckzero.service.d
printf '[Service]\nEnvironment=APPLAUNCH_MULTITASK=0\n' > ~/.config/systemd/user/cyberdeckzero.service.d/multitask.conf
systemctl --user daemon-reload && systemctl --user restart cyberdeckzero.service
```

- **If an app is stuck:** hold Esc 3 s to close the app in front, or from ssh
  `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl`.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie; the screen overlays must already be in `config.txt`):

```
tar xzf cyberdeckzero-launcher-0.8.0-arm64.tar.gz && cd bundle
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"
```

**Upgrading from 0.7.0:** run the same command; settings, app sources and installed apps carry over. Then update the
apps above in Settings > Apps to get them in the background. To preview: `sudo ./install.sh --dry-run`. To remove:
`sudo ./uninstall.sh`. The bundle includes a prebuilt Store from CardputerZero/Store. Built from the `release-0.8.0`
branch (main 8695c58 plus the release docs), version 0.8.0, channel `release`.

**Tested by the owner** (physical tests): _to be filled in before publishing._

**Known limits**

- **HackBerry CM5:** its touch screen reports at most two contacts, so there the switcher opens with Alt+Tab only,
  and Alt+Tab does nothing while an app without `X-Background` is in front. Its first-run hint still says "Hold three
  fingers".
- **viz1090** is not multitasking-capable and can hang on three-finger touches; hold Esc 3 s to close it.
- **Net Toolbox** stays a legacy app (the Close / Keep sheet, not kept in the background).
- Still true from 0.7.0: the Pi 3A+ DSI touch can stay dead after a soft reboot (a full power-off and on fixes it);
  viz1090 does not work on the Pi 3A+ DSI setup; wrapped stock apps draw a 320x170 picture scaled into their window.
