# Mesh Hop 0.3.0: deck test protocol (phase 3)

Run by the user on the deck (BT keyboard + touch). Board B (Seeed XIAO S3 WIO, MeshCore companion USB fw v1.15.0, `#test`
channel in slot 1) on USB. Answer "OK" or what was wrong, per step number. **[C]** = the Controller also checks it in the log
`~/.local/share/mesh-hop/mesh-hop.log` afterwards.

Package: `C:\CLAUDE\zero7\pkg-out\mesh-hop_0.3.0_arm64.deb` (local, not published). Once published, Settings > Apps > Update does the same.

## 1. Install over 0.2.2
1. With 0.2.2 installed and used (chats, contacts, settings set), copy the deb to the deck and run `sudo dpkg -i mesh-hop_0.3.0_arm64.deb`.
2. Open Mesh Hop. Expected: same chats and history, same contacts, groups, mute flags, scheduled advert and other settings as before. **[C]** no store or migration error.

## 2. Position box (Settings > Position row)
3. Select the Position row, press Enter: the Position box opens. Esc: closes, nothing changes. Tap the row: the box opens again.
4. Expected on board B: **no Board GPS switch** (the board does not list `gps`); the box says whether the board shares its position in adverts.
5. Type latitude `48.8566` and longitude `2.3522` (Tab between fields), OK. Expected: the box closes, the row shows `48.85660, 2.35220`. **[C]** `set position: ok`.
6. Open again, type `48,8570` and `2,3530` (comma separator), OK: the row shows the new position.
7. Open again, type latitude `95`, OK: red error "Latitude must be between -90 and 90", the box stays open, nothing sent. Same with longitude `200` and with `abc`. **[C]** no `set position` line for these.
8. Esc after typing a valid value: box closes, the position is unchanged.
9. Open again, Clear: the row shows `not set`. **[C]** `set position: ok`.
10. Top-level tab, hold Esc 3 s: the app quits to the launcher (a short Esc only shows the hint).

## 3. Heard back by N repeaters
11. Reopen the app. On `#test`, send a message. Expected: "sent", then within seconds "heard back by N repeaters" (if a repeater is in range). **[C]** `channel send seq=...: answer ... -> sent`, then `heard back on N routes`.
12. Watch for 60 s: N may grow while echoes arrive, then stops. Note the final N.
13. After those 60 s, quit (hold Esc 3 s) and reopen: the same message still shows the same N.
14. Send a direct message to a contact: it shows its own status (sending, sent, delivered or no ack), never "heard back".
15. Note: no repeater in range = the message stays "sent"; that is expected, say so.

## 4. Packet log (Settings > Packet log row)
16. Open the Packet log: capture is off, the list is empty.
17. Space (or S, or the Start button): capture starts; rows appear as packets are heard (send an advert or a `#test` message to help).
18. Up/Down select rows; the selected row shows its path and the raw bytes in hex below. Does `#test` traffic show the channel name?
19. C (or Clear): the list empties. Esc (or Close): back to Settings, the row says "capturing" or "stopped, N packets".
20. Quit and reopen: the Packet log row says "off", capture is off again.
21. Leave capture on long enough for a full list (500 rows; 15-30 min in a busy area) and scroll with Up/Down held, PgUp/PgDn, Home/End and touch drag. Is it smooth? Any lag on the rest of the app while capturing?

## 5. What to send back
* OK or the problem per step, with a screenshot of each problem (`scrot` or a phone photo).
* The log file `~/.local/share/mesh-hop/mesh-hop.log` (and `mesh-hop.log.1` if present).
* The N seen in steps 11-13, and whether a repeater was in range.
