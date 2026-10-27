# Report 043: PDA mode on large screens (short note)

The study looked at the PDA profile on screens bigger than the 4.3" panel, mainly the 7-inch FZ-M1
tablet (1280x800 panel, about 152 x 94 mm). What was decided, and what was built:

- **Portrait on a tablet needs no band change.** The band is the same on every panel: the writing boxes are one third
  of the height, the key strip sits on top of them (800x1280: 427 px and 51 px).
- **Landscape PDA is rejected.** PDA mode is portrait only; the study's landscape variants and its band sizing in
  millimetres were dropped.
- **The setting is built** (PR #102, v0.8.9): Settings > Screen > PDA mode, Off / On rotate left / On rotate right, on
  any screen whose short side is at least 480 px, that is not square and has room for the band. The stored choice wins
  over `board.conf`; a change restarts the launcher.

Owner test on the tablet: the three values and the restart work; portrait is "great". Not tested: rotate right on a DSI
board, the tablet's pen (a separate input device, unknown). Apps with their own band remain S4.

The guide is `docs/PDA-PROFILE.md` (Large screens: the tablet); the renders are the harness scenes `pda_tablet`,
`pda_tablet_terminal` and `pda_mode`.
