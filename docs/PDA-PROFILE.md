# The PDA profile

Since v0.8.8 the launcher can run upright, as a small handheld with a pen: a portrait screen, a handwriting band at the
bottom for text, and a single-stroke handwriting alphabet you can train on your own hand. This page says what it is,
how to turn it on and how to write with it. The pictures are renders made by the launcher's render harness at 480x800,
not photographs of the board.

## What it is

- A **profile you choose**, never detected. The landscape profile of the same board is unchanged.
- A portrait panel of 480x800 (the Raspberry Pi 3A+ with the Waveshare 4.3" DSI screen, turned) or 480x640 (3.5 to 4
  inch). The home grid has three columns, the status strip is drawn smaller and page titles show only the last part of
  a path ("Screen", not "Settings > Screen").
- Apps that declare a portrait size are shown as they are. A landscape app that fits is run turned by a quarter turn;
  compat apps and apps that do not fit are hidden.
- The app switcher is on, as on every panel whose short side is 480 px or more.

![Home grid, three columns](images/pda/pda-home.png)

## Install

On the Pi 3A+, add the board flag to the install command, and pass the same flag on every update:

```
sudo ./install.sh --board pi3a-wsdsi43-portrait
```

Then reboot. The other flags (`--keyboard-name`, and so on) are as in the [README](../README.md#quick-start).

## The band and the key strip

Where a text field asks for input, a band comes up at the bottom of the screen: a **key strip** over two writing boxes,
**abc** (letters) on the left and **123** (digits) on the right. Strokes drawn in a box are recognised and typed as
keys. Touches in the band never reach the app, and when the band is hidden the page gets the whole panel. A physical
keyboard hides the band, as it hides the on-screen keyboard.

The key strip has, from left to right: **Keyb**, **Tab**, **Ctrl**, **Alt**, the four arrows and **Enter**. Ctrl and
Alt are armed by a tap and apply to the next key (in the picture, Ctrl is armed in the terminal, so a letter written
next is typed as Ctrl plus that letter). **Keyb** puts the standard on-screen keyboard in the band's place; the
keyboard's close cross brings the band back.

| Text field and band | Pen ink | Terminal, Ctrl armed | Keyboard with its cross |
| --- | --- | --- | --- |
| ![Band under a text field](images/pda/pda-band-text.png) | ![A stroke in the letters box](images/pda/pda-band-ink.png) | ![Terminal, Ctrl armed](images/pda/pda-terminal-ctrl.png) | ![Standard keyboard](images/pda/pda-keyboard-cross.png) |

## The keyboard setting

Settings > System > On-screen keyboard has three values:

- **Auto**: the fallback on-screen keyboard shows only when no physical keyboard is present.
- **On**: the on-screen keyboard shows on text fields.
- **Handwriting + keyboard**: the band shows; Keyb switches to the standard keyboard. This is the default on the PDA
  profile, and the value on that page's picture.

Off no longer exists. A stored Off reads as Auto.

![Settings, On-screen keyboard](images/pda/pda-setting-keyboard.png)

## Writing with strokes

The alphabet is a single-stroke handwriting alphabet: each letter, digit and gesture is one stroke of the pen. The
shipped table is `share/strokes/glyphs.txt` in the bundle (installed as `/usr/share/APPLaunch/strokes/glyphs.txt`) and
is read-only. A glyph's strokes were traced by hand from a public reference chart into our own format; the table's
header says how.

- **Letters** are typed in lower case, written in the **abc** box. **Digits** are written in the **123** box.
- **A capital**: write an up stroke (bottom to top) before the letter. It applies to that one letter.
- **Caps lock**: two up strokes. A third turns it off.
- **Space**: a stroke from left to right.
- **Backspace**: a stroke from right to left.
- **Enter**: a stroke from top right down to bottom left.
- **Punctuation**: a tap puts the box in punctuation mode, then write the mark. The mode stays on after a refused
  stroke; a backspace stroke leaves it without deleting anything. A tap inside it is the period.
- **Extended characters** (rarer symbols, and X, which takes two strokes): start with a stroke from top left down to
  bottom right, then the glyph.
- **Accents** are written before the letter, as a dead accent that composes with the next letter.

The table is the reference for exact shapes; a stroke is judged on its shape and on where it starts, not on its size.

## Strokes training

Settings > System > Strokes training (PDA profile only) teaches the recogniser your own hand. The page keeps the band
shown and takes its strokes without typing. It shows the glyph to write with the chart stroke as a guide, and judges
four attempts per glyph: recognised, refused, or read as another glyph. Your attempts become personal templates that
the recogniser loads at once.

The **Pick** button opens a glyph picker: grey glyphs are not trained, green are trained, amber are hard (under 75 %).
The page also has Back, Skip, Redo, Reset all (it asks first) and Finish, which re-tests each trained glyph and then
shows a summary. It resumes where it stopped.

| Training | Glyph picker |
| --- | --- |
| ![Strokes training](images/pda/pda-training.png) | ![Glyph picker](images/pda/pda-training-picker.png) |

Two files are kept in `~/.config/applaunch/`:

- `strokes-user.txt`: your personal templates, in the table's format, loaded after the shipped table. You can add
  variants by hand; lines you write are kept.
- `strokes-training.txt`: every attempt, appended, for tuning. The PC tool `strokes-log` in `tools/strokes/` reads it.

## Limits

- Accuracy has only been measured on synthetic strokes made from the table's own templates (99.5 % correct on them,
  about 82 % under harsh noise). On the first board test 11 of 26 letters were refused before the training page
  existed. A capture session with a real stylus is planned.
- The physical size of the Pi 4 B's Freenove panel is a guess, to be checked with a ruler.
- Keys from the band do not reach compat apps (they are hidden on portrait).
- A turned app at rotation 180 has not run on a board.
