# Clipboard and text selection (step 1: P0 and P1; step 2: the touch bubble)

Owner decisions of 2026-11-26: build the clipboard and the keyboard-side selection now, the touch bubble and handles
later (they are reviewed as draft screens first); a 4 KB length-prefixed form besides the one-line verb. Step 2 (the touch
bubble, below) was then built as an overlay that appears only after a finger selection: no always-visible strip, no extra
row, no change to any screen where nothing is selected (an always-on Paste / Copy / Clear strip was built and reverted at
the owner's request; branch `clipboard-strip-parked`, do not bring it back). Word selection is a **double tap**, never a
long press (a long press is used for options in some apps). Research:
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
  selection. Esc ends the editing and the selection with it (it does not stop at "just clear the selection": Esc keeps its
  meaning everywhere); Up, Down, Tab and a tap elsewhere end it too.
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
  up) is ignored. The bubble and handles of step 2 hang on this tap.
- **OSK.** No change. Ctrl (one-shot) then a letter sends the key with `KBD_MOD_CTRL`, which the field reads. The OSK's
  arrow keys carry no Shift (a one-shot Shift is for the next character key only), so on the OSK alone the way to select is
  Ctrl then A.

Not touched, as agreed: Markdown Notes, Mesh Hop, RSS Reader (their own text input), the Serial Console terminal grid
(Ctrl+C there is a byte for the port). **Serial Console's line field is a `TextField` that already sends the on-screen keyboard's Ctrl
keys to the port** (`terminal_view.cpp`, `keyboard_event_cb`): with the new keys the same press would also copy, select all or
paste in the field. `TextFieldSpec::clipboard_keys = false` leaves Ctrl+A / C / X / V and the Insert / Delete shortcuts to the app
(Shift+arrows still select, nothing else changes); Serial Console must set it when it moves its pin (one line in its
`TextFieldSpec`, not done here). The render harness has a field "Raw" for it.

## Step 2: the touch bubble and the selection handles

Code: `ext_components/cp0_lvgl/src/cp0_ui_text_field.cpp` (the `touch_*` functions at the end), `include/cp0_ui_widgets.hpp`
(`TextFieldSpec::clipboard_touch`, `TextField::touch_ui()`), `include/cp0_ui_text_edit.hpp` (`word_range`,
`Buffer::select_range`, unit tested). Spec: `reticulum-work/text-selection-assessment.md` section 3.

**The flow** (nothing here is shown until a finger taps in the field):

| Step | What shows |
|---|---|
| Tap in the field | the caret goes to the tap (step 1) and a small handle appears under it; no bubble |
| Tap the caret handle | the bubble **[Select all] [Paste]** (Select all only when there is text, Paste only when the clipboard holds text; neither: no bubble). A second tap on the handle closes it |
| Select all, or a **double tap** on a word | the range, a handle at each end, and **[Cut] [Copy] [Paste] [All]** (Paste only with text on the clipboard, All only when not everything is selected) |
| Drag a handle | that end moves (the finger keeps its offset from the edge; a handle never crosses the other: at least one character stays selected); the bubble steps aside while the handle is held and comes back on release; the caret handle drags the caret the same way |
| Copy | the button says "Copied" for 1.5 s (same button, same size: it was measured for the longer text), then the bubble closes; the selection and its handles stay, a tap on a handle opens the bubble again |
| Cut, Paste | as the keys (step 1); the selection is gone, the caret handle is left under the new caret |
| Password field | Paste (and Select all, All) only: no Cut, no Copy |
| Any key (physical **or** on the keyboard), Esc, a press elsewhere on the screen (not on the field, the bubble, a handle or the keyboard), the editing stopping | everything is put away; a press elsewhere also drops the selection. Typing over a selection still replaces it (the keyboard's own presses are not "elsewhere") |

**Double tap** (decision of 2026-11-27). The first tap places the caret at once, as before: **no delay for a single tap**.
A second tap within **500 ms** and within half the TARGET token (3.5 mm) horizontally of the first selects the word under the
finger (a run of letters, digits, `_` and non-ASCII characters; a run of spaces; or a run of other punctuation, so a URL
selects piece by piece) and shows the range handles and the bubble. It is an accelerator: the route by the caret handle needs
no double tap and no hold (D-35). **No long press is used by the bubble feature.** A double tap on a field whose app turned
`clipboard_touch` off does nothing more than place the caret.

**Placement.** Everything is a plain object on the display's top layer (as the keyboard is), `CLICKABLE`, not `CLICK_FOCUSABLE`,
in no group (the password eye's rule), not scrollable and not a child of the page: the page's scroll cannot take a drag, a tap on
them never changes the focus, never ends the editing and never shows or hides the keyboard (the scenes check `expect_osk`
and `expect_editing` around every bubble tap). A 100 ms timer lays them out again only when something moved (the field, the
selection edges, the keyboard), so they follow a page that scrolls and a field that scrolls sideways, and a slow SPI panel
redraws little. A press anywhere is seen through an input-device event (`LV_EVENT_PRESSED` on the pointer indev), only while
the touch UI is shown.

- Handle: drawn as a 14 px dot on a 2 x 6 px stem at the character edge, in the editing blue; its **touch area is the TARGET
  token wide and high** (7 mm, at least 44 px), under the text line. Range handles hang to the left of the start edge and
  to the right of the end edge, so their areas never overlap (a one-character selection too).
- Bubble: buttons of the TARGET token height (width: the text and 2 x SPACE_M, at least TARGET), 1 px dividers, dark
  with a light edge; no shadow, no animation, no fade. Centred on the selection and clamped into the screen with a
  SPACE_S margin.
- Vertical: above the field if it fits under the header; else below the handles if the keyboard leaves room (800x480 with
  the keyboard up and a field near the top, or no keyboard); else, **with the keyboard up and no room**, over the label row
  and the field's top padding, above the text line, with the **buttons squeezed down to 44 px at least**
  (`CP0_TOK_TARGET`'s floor): 640x480 (59 px bubble, target 79), 480x320 (46 px) and 320x480. The bubble never covers the
  header (Esc, title, status strip) or the keyboard.
- When the keyboard leaves less than 7 mm under the text line (480x320, 320x480 and 640x480 with it up) the handle's
  touch area is cut at the keyboard's edge, and may rise no higher than the middle of the text line so that a tap on the
  text still reaches the field: 28 to 47 px high instead of 7 mm there. This is the one place the 7 mm recommendation
  (R-4) is not met; it needs the owner's finger on a board.
- A handle whose edge has scrolled out of the field, or a field outside the screen's body, is hidden.

**Opt-out.** `TextFieldSpec::clipboard_touch = false` (default `true`): no bubble, no handles, no double tap; a tap still
places the caret. `clipboard_keys` is separate (the keys). **Serial Console's line field must set both to `false`** when
it moves its toolkit pin (two lines in its `TextFieldSpec`, not done here): its on-screen Ctrl keys go to the port. The
render harness's field "Raw" has both off.

**Deviations and limits.** The handle drag has no edge auto-scroll of its own: the field's own scroll follows the caret,
so a drag past the right edge moves a character per event. A press that finds nothing clickable under it (no object at
all) is not seen by the input-device event, so the touch UI stays; every page has a clickable body or screen today.
A short handle area on the tightest layouts, and the squeezed bubble, are above. Not verified on a board: the feel of
the handles, whether a finger reaches the 28 px handle areas next to the keyboard, SPI redraw cost.

Scenes (headless, with injected touch; the harness checks the geometry of every shown bubble and handle: inside the
screen, under the header, above the keyboard, off the text line, touch areas, range handles apart): `clipboard_bubble`
(800x480, 640x480, 480x320 with the keyboard up), `p320_clipboard_bubble` (320x480), `clipboard_bubble_nokbd` (a physical
keyboard, no on-screen keyboard); commands `doubletapchar`, `taphandle`, `tapbubble`, `draghandle`, `expect_bubble`,
`expect_handles`, `expect_editing` ([render README](../../projects/APPLaunch/tests/render/README.md)). No golden shot shows a
bubble, so every existing golden is unchanged (`run.sh`: 315 identical).

## Tests

| What | Where |
|---|---|
| Protocol, expiry, framing parse, rate limit, **log never has the text** | `projects/APPLaunch/tests/test_app_request.cpp` |
| The real server and the real client: both forms, 4096 bytes, pieces, a payload that holds a request line, a bad terminator, an oversize announcement, a client that leaves mid-payload, old launcher, no launcher, slow launcher, **the log grep** | `projects/APPLaunch/tests/test_app_request_server.cpp` |
| Selection and byte mapping: multibyte, emoji, maximum length, paste cleaning | `ext_components/cp0_lvgl/tests/test_ui_text_edit.cpp` |
| Selection model: word range for a double tap, `select_range` | `ext_components/cp0_lvgl/tests/test_ui_text_edit.cpp` |
| Headless scenes at 800x480, 640x480, 480x320 (and 320x480 in `p320_clipboard`): keys, limits, tap, OSK Ctrl. The harness runs the real server on a short socket (`clipboard server on|off|old|slow`) | `projects/APPLaunch/tests/render/scenes/clipboard_keys.scene`, `clipboard_limits.scene`, `clipboard_touch.scene`, `clipboard_osk.scene`, `p320_clipboard.scene`; harness `src/harness_fields.cpp` |

## Where the apps are affected

Apps get all of this by moving `apps/_common/LAUNCHER_REF` to a launcher commit that has it and rebuilding (no per-app code).
The shared-`TextField` users: Cassette (dialog), Deck Atlas, Home Assistant, Klondike Solitaire (dialog), Net Toolbox,
Reticulum (conversation and edit fields), Serial Console (line field), VHS (dialog), Vinyl (library, share form, view),
viz1090, the template. Home Assistant (URL and token), RSS Reader's feed URL (its own input, not covered), Reticulum (address)
and Net Toolbox (host) are where paste matters first. Reticulum's `copy_address` / `paste_address` shell out to
`wl-copy` / `wl-paste` today and can move to `cp0_app_clipboard_*`.

## Where Copy and Paste buttons would be wanted (not built)

Step 2's bubble covers editable fields on touch. Still without a visible control, and not built: a Copy on read-only values
(Reticulum "My address", Net Toolbox public IP and results, Deck Atlas coordinates), which are not text fields. A Paste
next to an address, URL or token field and a Clear are not needed any more for a touch user: tap the field, tap the handle,
Paste. The label of any such button must not repeat a hold (D-35).
