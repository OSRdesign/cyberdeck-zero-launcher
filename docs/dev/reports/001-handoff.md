Task: 001 Wi-Fi survey (round 2, after report 004)        Agent: apps-dev        Result: done (built; not run on the deck)

Changed:
- `cyberdeck-zero-apps/apps/wifi-survey/src/main/ui/wifisurvey.cpp` (all three fixes; `wifisurvey.hpp` unchanged this round)
- `cyberdeck-zero-apps/apps/wifi-survey/root/usr/share/APPLaunch/bin/M5CardputerZero-wifi-survey` (rebuilt aarch64 binary)
- `cyberdeck-zero-apps/packages/wifi-survey_0.1.0_arm64.deb` (rebuilt, md5 9f54fe19a539417824c6b0d0f9a4c7a4, 1455828 bytes,
  sha256 b5bab1bc0720ac1a5da8ca559ea30c4eb0bf7c38d7bc1b752bd9d0f1b2b1546a)
- `cyberdeck-zero-apps/registry.json` (only the wifi-survey entry is new / updated)
- Not changed: launcher source, .desktop, README (docs-writer), lanscan and viz1090 (debs and registry entries).

Fixes:
1. dBm column. The cell now shows only the number ("-66", worst case "-100"), right aligned in a 34 px cell (the digits
   of "-100" are about 26 px at 12 pt, so no ellipsis is possible). The unit moved to a column-title strip
   ("Network  dBm  Ch  Band  Security") drawn above the rows, 14 px high, inside the Networks box (hidden with it).
   Why this and not the status/title bar: the header bar is full (tabs left, "N networks / scanning" status right), and a
   title strip also names the other columns, so every cell stays fully readable and is explained. The vertical room
   came from the row height 19 -> 17 px (18 header + 14 titles + 6 x 17 rows + 16 footer = 150 px, exactly the content
   height; the 14 pt row font is 16 px tall, so the row text is still not clipped; a tap target is 34 px on the deck).
   Other columns: SSID 106 px (was 96), signal bar 20 px, channel 26 px, band 46 px, security 56 px (all wider or equal).
2. R key. Cause found: the raw-key listener (`LV_EVENT_KEYBOARD`, which tells a real R from the Right arrow, both
   arrive as keycode 19 on the normal path) was registered on `lv_screen_active()` inside the page constructor. The page
   screen (`root_screen_`) is only loaded after the constructor returns (`ui_app_page.cpp`, `lv_screen_load(page.screen())`),
   so the listener sat on another screen and never fired: R reached the normal path as Right and toggled the tab. It is now
   registered on `root_screen_`, which is the active screen when keys arrive. The "this is an R" window (300 ms) is also no
   longer cleared on first use, so a held R (repeat events) is ignored by the Right path too; repeat and release of R only
   extend the window, only the press starts a scan.
3. Detail follows the BSSID. The detail view already refreshed its data by BSSID on every scan; what looked like "another
   network" in report 004 was mostly the R key moving the selection (R = Right = next network in the detail view), which
   fix 2 removes. Still hardened: Left/Right in the detail now step from the network shown (found by BSSID), not from a
   stale list position. If the network disappears from a scan, the last values stay on screen, the title turns red with
   " - gone" and the status row reads "- gone (last values)"; if it comes back it updates and the mark disappears.

How it was built:
- `wsl -e sh /mnt/c/CLAUDE/zero7/cyberdeck-zero-apps/apps/wifi-survey/build/build.sh` (flock /tmp/wsl-build.lock inside),
  rc=0, ELF 64-bit ARM aarch64. Then `python tools/make_registry.py --owner OSRdesign --repo cyberdeck-zero-apps`; it
  rebuilt all three debs (timestamps change their bytes), so `git checkout` restored lanscan and viz1090 debs and I put
  their registry entries and `generated_at` back to the previous values.
- `dpkg-deb -I`: wifi-survey 0.1.0 arm64, Depends network-manager. `dpkg-deb -c`: .desktop, binary, icon, oui.tsv, copyright,
  only under /usr/share/APPLaunch and /usr/share/doc/wifi-survey.
- md5: lanscan c66f2ad8b73e95ea7f68767ed3048f61, viz1090 e32c03bebab1984b0ecd6b6e26bca2d6 (unchanged, equal to their
  registry entries); wifi-survey 9f54fe19... equals its registry entry. `git status` of the apps repo: `registry.json`
  modified, `apps/wifi-survey/` and the new .deb untracked.
- Not run on the deck, no screenshot: the fit is computed from font widths, not seen.

Not done / risks:
- Layout is unseen. The 14 px title strip and 17 px rows are the tightest part: look for clipped glyphs (descenders in
  "Network", "Security", the 14 pt SSID text in 17 px rows).
- The R fix is a code-level diagnosis (screen not yet active when the listener was added); it needs the real key test.
- "WPA2-EAP" can still end with an ellipsis (accepted earlier). The "5 GHz no networks seen (...)" line on the Channels
  screen is still clipped on adapters without 5 GHz (cosmetic, not in this round).

What the verifier must re-check (install the .deb above, md5 9f54fe19a539417824c6b0d0f9a4c7a4):
1. Networks, worst-case rows through the nmcli stub: "-100" (full, no ellipsis), a signal 0 % row, channel 165 (3 digits),
   channel 100, WPA2/3, WPA3, open, WPA2 802.1X, 5 GHz rows with "5 GHz", the connected row. Column titles
   (Network, dBm, Ch, Band, Security) readable and aligned over their columns; no clipped text in titles or rows. Full-screen
   screenshot including one 5 GHz row and the connected row.
2. R key (real Bluetooth keyboard, or the uinput helper with KEY_R=19): on Networks, on Channels and in a detail view, R only
   rescans (status shows "scanning") and never switches the screen or moves the selection; held R the same. Right arrow,
   Left arrow and Tab still switch Networks/Channels; in a detail Left/Right still go to the previous/next network.
3. Detail across a refresh: open a detail, wait at least two scans (>= 10 s) or press R: it keeps showing the same BSSID.
   With the stub, change the list order/number of rows between scans, then remove the open network from the stub output:
   title red " - gone", status "- gone (last values)", no crash; put it back: it updates again. Esc returns to the list.
4. Regression: launch by Enter and by tap (no self-opening detail), tap a row opens the detail after 1 s, tap on tabs and on
   the status text, swipe scroll of the list (rows are now 17 px), Esc quits, install and removal as in report 004.
5. `git status` of the apps repo as above; lanscan and viz1090 md5 as above.

Open questions: none for the Controller. The README entry is still pending with docs-writer.
