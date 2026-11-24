# Quick controls: board tests (owner, a few minutes)

Built-in tile "Quick controls" on the Home grid; page "Controls" with Shutdown, Reboot, Wi-Fi, Bluetooth. No confirmation:
Shutdown and Reboot act at once. Test on any board; do 1-6 on the deck, 7-8 on a board you can power-cycle, keep a
keyboard or SSH session ready for Wi-Fi.

1. Home: the coral "Quick controls" tile is after CLI and the name fits on one line. Open it (touch or Enter): the
   page shows four buttons, Wi-Fi and Bluetooth with a dot and On / Off, the focus ring on Wi-Fi. Nothing was switched
   by opening it (check the status strip).
2. Keyboard: arrows move over the 2 x 2 grid (no wrap), Tab and Shift+Tab go round (Shutdown, Reboot, Wi-Fi, Bluetooth, wrapping), Esc and the header Esc go back to Home.
3. Wi-Fi: Enter (or tap) switches it off: "Switching..." then "Off" (ring), the strip's Wi-Fi bars go. Again: "On".
4. Bluetooth: the same; the strip's Bluetooth icon follows. Pair state of a connected keyboard is not asked for.
5. Not available: on a board without Wi-Fi or Bluetooth (or with the adapter removed) that button is dimmed, says
   "Not available", does nothing.
6. Tile hiding: Settings > Launcher > Quick controls Off removes the tile; On brings it back.
7. Reboot: tap Reboot: the page says "Restarting..." and the board reboots with no question asked.
8. Shutdown: tap Shutdown: "Shutting down...", screen off, the board powers off (Pi: green LED stops).
9. Touch slip: press on Home's tile, hold, then open the page by lifting; nothing on the page fires from that lift.
Test 3 (Wi-Fi) and 7 (the "Restarting..." message) at the board itself, on its own screen and keyboard: an ssh session
over wlan0 drops when Wi-Fi goes off or the board reboots, so they cannot be watched from ssh; use Ethernet or the
console to look, or just watch the panel.
Report per number: OK, or what you saw.
