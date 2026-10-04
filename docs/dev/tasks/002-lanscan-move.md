# 002 - Move LanScan source into the apps repo

Status: done (verifier PASS in report 006; lanscan 0.1.1 installed on the deck; README updated; touch accepted by the
user; `projects/LanScan` removed from the launcher repo). Owner: apps-dev. Verifier: deck-verifier. Docs: docs-writer. Acceptance: the user, on the deck.

## Goal
LanScan's source moves from the launcher repo (`projects/LanScan`) to the apps repo (`apps/lanscan/src`), built the
same way as Wi-Fi Survey (`apps/wifi-survey/build/build.sh`: copy source into a launcher worktree tree, scons under the
build lock, copy the binary into `root/`). This removes the last app source from the launcher repo.

## Acceptance criteria
- `apps/lanscan/src` holds the full source; `apps/lanscan/build/build.sh` rebuilds the binary from it.
- The rebuilt `lanscan` package behaves the same as the current one: same package name, share code, title, category,
  icon, desktop file, version policy per `checklists/app-packaging.md` (bump the version if the binary changes).
- `registry.json` entry updated for the new package; `viz1090` and `wifi-survey` debs and entries unchanged.
- `projects/LanScan` removed from the launcher repo only after the app is verified on the deck and the user agrees
  (the Controller does that step); the launcher must still build without it (check the launcher build, any references
  in the launcher's scons/Kconfig/docs).
- Hand-off report saved as `docs/dev/reports/002-handoff.md`.

## Constraints
- Do not change LanScan behaviour or UI. Pure move plus build script.
- Do not touch the launcher source in this task (list the references that will need removing instead).
- No commit or push.
