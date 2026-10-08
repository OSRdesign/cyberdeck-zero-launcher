# Mesh Hop 0.1.0 - user feedback after the first test on the deck (2026-10-07)

Tested on the deck with the real board connected. Not implemented yet: the user first wants the feature list below
decided (see the Controller's question), then one batch of work.

## Passed
Display, fonts, colours, top bar, hold-Esc exit (1, 2), tab touch (3), row touch and drag-scroll (4), touch axes (6),
Esc rules (8), compose with accents (9), Tab (10), keyboard sleep notice (11), board connect and sync (12),
contact SNR / hops (7), unplug / replug (15). Direct messages send and show delivery.

## Problems and requests
1. **Unread count** is not shown on the "Chats" tab title.
2. **Channel list (left pane):** do not show the last message; show the number of new (unread) messages, as for direct messages.
3. **Edge swipe (Back)** does not work. Not needed: direct touch on elements is enough (may be removed).
4. **Contacts sort:** slow (161 contacts) - show a "Loading" popup while the list changes. After sorting by anything other than
   the default (name), tapping a node reverts the list to the default order and opens the node at the tapped position of the
   other order: the tap must open the node shown, and the chosen sort must be kept.
5. **F1..F5** do nothing on the user's keyboard layout (Tab works). **Up/Down in the contact list is slow** with 161 contacts
   (touch scroll is fast).
6. **Channels:** a sent message stays on "sending" (never turns to sent/delivered), even when an answer arrives. Direct works.
   Wanted on both channels and direct: **the number of repeaters that heard the sent message**.
7. **Settings:**
   - the `-` / `+` buttons are useless (too small);
   - fixed-choice fields (Bandwidth, Spreading factor, Coding rate, Preset): a popup with the value in the middle and a
     left and a right arrow button on each side to step through the values, no free entry; Preset must list ALL presets of the
     MeshCore firmware;
   - the Public channel (fixed, useless) should not be listed in Settings; channels must be removable;
   - "Undo changes" and "Save to radio" at the end of the list, and both ask for confirmation with a Yes / No message box;
   - if the firmware / board supports GPS, an option to activate it.

## Method note (user)
"Do not always propose and implement solutions based on my feedback": there are many small features in the reference
clients that Mesh Hop does not have. List them all and ask which ones are mandatory for a good MeshCore experience before
planning the work.
