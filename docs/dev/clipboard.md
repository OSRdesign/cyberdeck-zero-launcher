# Clipboard and text selection (step 1: P0 and P1)

Owner decisions of 2026-11-26: build the clipboard and the keyboard-side selection now, the touch bubble and handles
later (they are reviewed as draft screens first); a 4 KB length-prefixed form besides the one-line verb. Research:
the two reports of 2026-11-26 (`reticulum-work/open-topics-report.md` topic 1 and `text-selection-assessment.md`).

## P0: the clipboard (launcher side)

`launcher.sock` verb `clipboard`, specified in [`docs/APP-CONTRACT.md`](../APP-CONTRACT.md) section 14 ("The clipboard").
Code: `projects/APPLaunch/main/ui/app_request.{hpp,cpp}` (the `Clipboard` slot, the verb, `payload_length`,
`handle_payload`), `app_request_server.{hpp,cpp}` (the length-prefixed framing), `app_requests.cpp` (wires both),
`board_facts.cpp` (`clipboard` in `requests=` and `hello`'s `verbs=`), client `ext_components/cp0_lvgl/src/cp0_app_client.c`
(`cp0_app_clipboard_set/get/clear`), `projects/APPLaunch/pizero2w/ctl/cyberdeckzero_ctl.c` (`clipboard set|get|clear`).

Decisions:

| Question | Choice | Why |
|---|---|---|
| Framing for text that is long or has newlines | `clipboard set-len <n>`: the request line, then exactly n bytes, then one newline; `get-len` answers `ok <n>`, a newline, the bytes, a newline | The socket is line based, with a 254-byte line limit and a 256-byte client buffer. A length prefix needs no escaping (a tab, a backslash, a newline, any UTF-8 comes back byte for byte), no expansion (base64 costs a third more) and tells the receiver how much to wait for. Base64 on one line would still need a line far over the limit, so it needs the same server exception and an encoder on both sides. The cost is a small state machine in the server (`need` and `pending` per connection), covered by the socket test. The final newline in both directions is a check byte: a payload that is not followed by a newline means the framing is lost and the connection is closed. |
| The one-line form | `clipboard set <text>` (up to 240 bytes), `get` for a text that is one short line | Fits the style of the other verbs, usable from a shell and by a client that has no framing code. `get` answers `err size` for anything else, `get-len` always works. The toolkit uses the one-line form for a short single line and `set-len` for the rest, and always asks `get-len`. |
| Expiry | 10 minutes after the last `set`; a read does not extend it | Owner's report proposal. Checked at the next request, so a text stays in memory (zeroed when found) until someone asks; the launcher is the only reader of its own memory, and it exits clear. A timer thread was not worth it. |
| Logging | the line of a `clipboard` request is never logged; the log has `[req] app: clipboard set 37 bytes`, `set-len N bytes`, `get`, `get-len`, `clear`. Requests refused before they are understood (too long, a control character, rate limited, a bad `set-len` announcement) leave no log line at all | `handle_line` logged every known verb's whole line. The exemption is in `app_request.cpp`; `test_app_request.cpp` and `test_app_request_server.cpp` run a secret through every form and every error and grep the logged lines. |
| Rate | class `clipboard`, one per 50 ms per client process (`err limit`, dropped before it reads anything) | The default class is 20 ms; 50 ms keeps a flood of 4 KB sets cheap and is far below a human's Copy and Paste. |
| Who may read | any process of the user, as for every verb | The existing model ("an agreement, not a wall"). The contract says an app reads it only on an action of the user. |
| Memory | `Clipboard::set` zeroes the old text; the server zeroes the receive buffer and its payload copy; the client zeroes its buffers | Best effort; not a guarantee against a memory dump. |

An older launcher answers `err unknown`; the client says `CP0_APP_CLIPBOARD_UNSUPPORTED`. Beforehand, `clipboard` is in
`board.state` `requests=` and in `hello`'s `verbs=`. `requests=` on a board now reads `hello,caps,toast,beep,clipboard,...`.

## P1: the TextField selection model

Code: `ext_components/cp0_lvgl/include/cp0_ui_text_edit.hpp` (header only, no LVGL: `text_edit::Buffer`, the paste
sanitiser), `src/cp0_ui_text_field.cpp` (keys, highlight, clipboard calls, tap), `include/cp0_ui_widgets.hpp`.

- **Model.** `Buffer` = UTF-8 text, caret and anchor as byte offsets that always sit on character boundaries; selection =
  the range between them. The textarea shows it: the cursor at the caret, the label's own text selection
  (`LV_LABEL_TEXT_SELECTION`) for the highlight, in character indices through `char_index` / `byte_offset`.
- **Highlight.** The label selection feature is on in every build that matters: the launcher's board build
  (`CONFIG_V9_5_LV_LABEL_TEXT_SELECTION=y` in `build/config/global_config.mk`, the Kconfig default is `y`), the render
  harness (`tests/render/config/lvgl_config.h`) and therefore the apps, which build against the launcher's LVGL
  configuration (`apps/_common/LAUNCHER_REF`). The code is guarded by `#if LV_LABEL_TEXT_SELECTION`: with it off, the
  model and the keys work and nothing is drawn. Enabling it costs a few hundred bytes of code and one `uint32_t` pair per
  label. Colour: accent blue `#2D9CDB` behind `#101010` text, both on the label's `LV_PART_SELECTED` (visible on the dark
  field `#1C1C1C`).
- **Keys.** Shift+Left/Right/Home/End extend; plain Left/Right collapse a selection to its near end; Ctrl+A all;
  Ctrl+C or Ctrl+Insert copy; Ctrl+X or Shift+Delete cut; Ctrl+V or Shift+Insert paste. By key code, on a press only
  (a held key does not repeat them), with Alt or Logo they do nothing. Typing, Backspace, Delete replace or delete the
  selection. With a selection Esc takes two steps (step 2): the first clears it, the second ends the editing; without a
  selection one Esc ends the editing, as before. Up, Down, Tab and a tap elsewhere end it too.
- **Copy with nothing selected: the whole field.** The owner's suggestion, kept: it makes "copy this address" one key and
  one tap on the OSK's Ctrl. Not risky for passwords because a Password field never copies at all; for a Text field the
  user chose to press Ctrl+C. **Cut with nothing selected does nothing** (a cut that empties a field by accident is worse
  than a copy that copies too much), and **a Cut deletes the text only once the clipboard took it**: with an old launcher
  or a slow one Ctrl+X leaves the field as it was.
- **Paste.** Replaces the selection; the clipboard text is cleaned (single line: CR and LF dropped like an HTML input, a
  tab becomes a space, other control characters and bad UTF-8 dropped; a Number field keeps the digits; a multi-line field
  keeps LF and tab) and **cut at a character boundary to the field's `max_bytes`** (typing is still refused when full, as
  before). A paste that cannot take even its first character leaves the selection.
- **Clipboard calls from the UI thread.** `cp0_app_clipboard_*` with 100 ms. After a "no service" (no socket, older
  launcher, timeout) the next try waits 3 s, so a held key or a stalled launcher costs the UI at most 100 ms in 3 s.
  No fallback clipboard inside the app: on an old launcher Copy and Paste do nothing, silently, as the contract says.
- **Touch.** A tap (the box's `CLICKED`, pointer indev only) puts the caret at the tapped character with
  `lv_label_get_letter_on` on the textarea's label (its coordinates include the horizontal scroll, as LVGL's own textarea
  hit test does) and ends a selection; a tap whose point is no longer in the field (the page moved when the keyboard came
  up) is ignored. No bubble and no handles (later); the Paste / Copy / Clear strip is step 2, below.
- **OSK.** No change. Ctrl (one-shot) then a letter sends the key with `KBD_MOD_CTRL`, which the field reads. The OSK's
  arrow keys carry no Shift (a one-shot Shift is for the next character key only), so on the OSK alone the way to select is
  Ctrl then A.

Not touched, as agreed: Markdown Notes, Mesh Hop, RSS Reader (their own text input), the Serial Console terminal grid
(Ctrl+C there is a byte for the port). **Serial Console's line field is a `TextField` that already sends the on-screen keyboard's Ctrl
keys to the port** (`terminal_view.cpp`, `keyboard_event_cb`): with the new keys the same press would also copy, select all or
paste in the field. `TextFieldSpec::clipboard_keys = false` leaves Ctrl+A / C / X / V and the Insert / Delete shortcuts to the app
(Shift+arrows still select, nothing else changes); Serial Console must set it when it moves its pin (one line in its
`TextFieldSpec`, not done here). The render harness has a field "Raw" for it.

## Tests

| What | Where |
|---|---|
| Protocol, expiry, framing parse, rate limit, **log never has the text** | `projects/APPLaunch/tests/test_app_request.cpp` |
| The real server and the real client: both forms, 4096 bytes, pieces, a payload that holds a request line, a bad terminator, an oversize announcement, a client that leaves mid-payload, old launcher, no launcher, slow launcher, **the log grep** | `projects/APPLaunch/tests/test_app_request_server.cpp` |
| Selection and byte mapping: multibyte, emoji, maximum length, paste cleaning | `ext_components/cp0_lvgl/tests/test_ui_text_edit.cpp` |
| Headless scenes at 800x480, 640x480, 480x320 (and 320x480 in `p320_clipboard`): keys, limits, tap, OSK Ctrl. The harness runs the real server on a short socket (`clipboard server on|off|old|slow`) | `projects/APPLaunch/tests/render/scenes/clipboard_keys.scene`, `clipboard_limits.scene`, `clipboard_touch.scene`, `clipboard_osk.scene`, `p320_clipboard.scene`; harness `src/harness_fields.cpp` |

## Where the apps are affected

Apps get all of this by moving `apps/_common/LAUNCHER_REF` to a launcher commit that has it and rebuilding (no per-app code).
The shared-`TextField` users: Cassette (dialog), Deck Atlas, Home Assistant, Klondike Solitaire (dialog), Net Toolbox,
Reticulum (conversation and edit fields), Serial Console (line field), VHS (dialog), Vinyl (library, share form, view),
viz1090, the template. Home Assistant (URL and token), RSS Reader's feed URL (its own input, not covered), Reticulum (address)
and Net Toolbox (host) are where paste matters first. Reticulum's `copy_address` / `paste_address` shell out to
`wl-copy` / `wl-paste` today and can move to `cp0_app_clipboard_*`.

## Step 2: the clipboard strip and the Copy button

Owner-approved design of 2026-11-26 (canvas boards 38 to 47). Code: `cp0_ui_text_field.cpp` (`TextField::build_strip`,
`update_strip`, `show_copied`, `CopyButton`), `cp0_ui_widgets.hpp`, `osk::content_changed()` in `cp0_ui_osk_widget.cpp`.

- **The strip.** While a field is edited, a row of three pills under it, inside the field's row: Paste, Copy, Clear. Each is one
  ROW token high (7 mm), the status strip's look (grey `#262626`, white text; lighter while pressed; darker with grey text when
  greyed). They take no keyboard focus (no `CLICK_FOCUSABLE`, in no group), so a tap leaves the field and the on-screen keyboard as
  they are; none is a long press. It shows from the moment the field starts editing to the moment it stops. `TextFieldSpec::
  clipboard_strip = false` leaves it out (Serial Console's line field will want it, with `clipboard_keys = false`).
- **States.** Paste: greyed unless the clipboard is known to hold text. Copy: the selection, or the whole field; greyed when the
  field is empty or the launcher has no clipboard; absent on a Password; "Copied" in green for 2 s afterwards (the pill keeps its
  width). Clear: only when the field has text; empties it (a Password's bytes are zeroed) and keeps the editing going.
- **What the strip knows of the clipboard.** One 8-byte `get` when a field starts editing, and what every copy, cut and paste
  learns (`ClipAvail`). No timer: another app cannot change the clipboard while this one is in front. An old launcher, no socket and a
  stalled launcher read as "absent" (Paste and Copy greyed); the toolkit's 3 s backoff applies, and the next field to start editing asks again.
- **Layout.** The strip is the row's last child (under a label-above or label-beside field alike: a beside row wraps). A page keeps
  the field in view (`Page::on_inset`), a dialog lays itself out again (`Dialog::relayout`): `osk::content_changed()` runs the inset
  listeners when the strip comes or goes. When a press elsewhere ends the editing (a button or a row below the field) the strip stays
  until the finger is up, so nothing moves under it between press and release.
- **Esc.** With a selection the first Esc clears it (the field is still edited, the strip stays); the second ends the editing.
- **CopyButton.** For a value that is shown, not edited (an address, a public IP, a coordinate): `CopyButton(parent, layout, value_fn,
  with_label)`, round with the copy glyph or a pill "Copy"; a tap copies `value_fn()`, shows a green check (pill: "Copied") for 2 s;
  greyed when the value is empty or the launcher has no clipboard. The page places it (`obj()`).
- **Where there is no room (owner decision 2026-11-26).** With the on-screen keyboard up, the room above it can be too short for the
  field's row plus a 7 mm strip (640x480: about 120 px above the keyboard, 480x320: about 80). `TextField::fit_strip()` (an inset
  listener while editing) then shrinks the pills to what is left, never under 32 px (`kStripFloorPx`), scales their text with them,
  drops the label above the field and the gaps to a minimum, and puts the row at the top of the viewport, so the strip is fully
  visible above the keyboard. Where it fits (800x480, 320x480, any size with a physical keyboard) the pills stay 7 mm. When the
  keyboard goes, the next edit starts at 7 mm again. Not solved: a viewport under field + 32 px + gaps (nothing today).
- **Tests.** Scenes `clipboard_strip`, `clipboard_strip_states`, `clipboard_esc`, `clipboard_strip_osk`, `clipboard_strip_osk_tight`,
  `clipboard_copy_button`, `p320_clipboard_strip`; harness commands `tapclip`, `expect_strip`, `copydemo` (`harness_fields.cpp`).

## Where Copy and Paste buttons are still wanted (not built)

Touch-only decks have no Ctrl key unless the OSK is up, and the OSK is never summoned by hand (D-38). The strip (shared fields)
and CopyButton (read-only values) now cover the fields and values of the shared toolkit; still open: the apps' own inputs (RSS
feed URL), apps placing a CopyButton on their result rows and read-only values (Reticulum "My address", Net Toolbox public IP and
results, Deck Atlas coordinates), and the Select all / Cut / Copy / Paste bubble (P2) in any editable field. The label of such a button must not
repeat a hold (D-35).
