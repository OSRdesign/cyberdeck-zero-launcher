# deck-p4: the deck case of P4's first board test (2026-11-01)

Not a capture: written by hand from the deck's log. 22 safe security and Raspberry Pi updates, the launcher
libraries curl, libcurl4t64 and libssl3t64, a dependency chain (chain-tool needs the new libchain1, which needs the new
libssl3t64) and a package that needs new boot firmware (raspberrypi-sys-mods -> raspi-firmware).

- `pulls.txt`: what `apt-get install --only-upgrade NAME` also upgrades: since apt 2.7 the other installed packages
  of the same source (libcurl3t64-gnutls -> curl, libcurl4t64: what made the helper refuse on the deck), and new
  dependencies. The model test, the render harness and the helper test compute the closure from it.
- `records.txt`: `apt-cache show --no-all-versions` stanzas (Source, Depends, Installed-Size) for the suspects.
