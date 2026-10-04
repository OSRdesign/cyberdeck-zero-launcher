# 003 - viz1090 touch screen detection

Status: done (verifier PASS in report 006; viz1090 0.1.1 installed on the deck; apps README updated; finger touch
accepted by the user). Owner: apps-dev. Verifier: deck-verifier. Docs: docs-writer. Acceptance: the user, on the deck.

## Goal
Touch must work in viz1090 (intro screen and map) whatever the order of the input devices, including when the
Bluetooth keyboard is connected.

## Cause
The display layer (`apps/viz1090/build/viz_fb_shim.c`, built into `libviz_fb.so`) opened a fixed
`/dev/input/event1`. With the Goodix panel on event0, event1 was the infrared node of the RTL-SDR dongle (and the
keyboard when it took that number), so touch events never arrived. Report 006 confirmed it on 0.1.0: the intro
process held event1.

## Fix (viz1090 0.1.1)
- With `VIZ_TOUCH` unset, the shim scans `/dev/input/event0..31` for a device that reports ABS_MT_POSITION_X and Y
  and not KEY_A; a name with "goodix" or "touchscreen" wins over other multitouch devices. `VIZ_TOUCH` still
  overrides.
- If no device is found, or the device disappears, the scan is retried once a second; touch state is reset on every
  reopen. Before, the input stayed closed for good.
- Nothing else changed (swap/invert defaults, pinch, close button, keyboard path).

## Result
On the deck the app's own touch descriptor pointed to the Goodix node at every launch, with or without the keyboard;
the retry reopened it about 20 s after it became readable again; keyboard, Esc and the close button worked.
Touch events were synthetic (a test device); real finger touch is the user's check.
