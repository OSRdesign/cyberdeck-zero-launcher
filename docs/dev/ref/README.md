# Reference material for the MeshCore app (task 007)

Downloaded 2026-10-05 so agents without web access can read it. Do not edit; re-download to refresh.

| File | Source | Licence |
| --- | --- | --- |
| `companion_protocol.md` | https://github.com/meshcore-dev/MeshCore/blob/main/docs/companion_protocol.md (Companion Firmware v1.12.0+, last updated 2026-03-08; written for BLE, "still in development, may be inaccurate") | MIT (MeshCore, Scott Powell / rippleradios.com) |
| `payloads.md`, `packet_format.md` | same repo, `docs/` | MIT |
| `meshcore_cli.py` | https://github.com/meshcore-dev/meshcore-cli `src/meshcore_cli/meshcore_cli.py` (downloaded to check how `#name` channels are hashed: no case change) | MIT (meshcore-dev, checked 2026-10-06) |
| `meshcore_py/*.py` | https://github.com/meshcore-dev/meshcore_py `src/meshcore/` (packets, reader, serial transport, device / contact / messaging commands): a working reference client, the real source of truth when the doc and the firmware disagree | MIT (Florent de Lamotte) |

USB serial framing (from `meshcore_py/serial_cx.py` and the MeshCore wiki): device-to-app frames start with `0x3E` (`>`),
app-to-device frames with `0x3C` (`<`), then a 2-byte little-endian length, then the frame (max 172 bytes). The first
command after connecting is CMD_DEVICE_QUERY (22), then CMD_APP_START (1); CMD_GET_CONTACTS (4) streams the contacts.
Default serial speed for the companion USB firmware is 115200 (check `serial_cx.py`).

Not to copy from: **wadamesh** (https://github.com/ALLFATHER-BV/wadamesh, GPL-3.0-or-later). It is the model for the
experience the user wants (touch UI for MeshCore: chat, contacts, channels, map with offline tiles, settings), but its
code is GPL and the apps repo is MIT: take ideas of the UX only, write our own code from the protocol docs.

## Other MeshCore clients evaluated (2026-10-06): none runs as is on the deck; reference only

| Client | Stack | Licence | Display needs | Verdict for the deck |
| --- | --- | --- | --- | --- |
| wadamesh (ALLFATHER-BV/wadamesh) | ESP32 firmware, LVGL, runs on its own touch devices | GPL-3.0-or-later | its own screen | UX model only; no code can be copied into our MIT apps |
| Meshy (codeberg.org/sesivany/meshy) | Python, GTK4 + libadwaita, GStreamer, libshumate | GPL-3.0-or-later | X11 or Wayland (GTK4 has no framebuffer backend) | needs a display server we do not have, heavy; UX ideas only |
| MeshCore Open (github.com/zjs81/meshcore-open) | Flutter / Dart 3.9 (BLE, USB serial, TCP), v9.5.0, 629 stars | MIT | GTK embedder (X11/Wayland) on Linux; flutter-pi needs KMS | not as is; MIT, so features and protocol handling may be studied and re-implemented with attribution |
| MeshZero (ours) | C++ / LVGL, serial companion protocol | MIT | the launcher's compat window | the path we continue (shelved as a draft) |

The deck has no display server and a legacy `/dev/fb0` (no KMS); the launcher draws straight to the framebuffer.
