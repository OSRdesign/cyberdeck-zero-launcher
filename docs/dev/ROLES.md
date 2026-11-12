# Roles and workflow

Lean setup: one **Controller** (the main Claude Code session, the only one who talks to the user) and four
subagents defined in `.claude/agents/`. Since 2026-11-12 testing is **Controller-led**: the Controller checks the
code and deploys, the owner runs the physical tests from the Controller's written protocol, and `deck-verifier` is
called only when the owner asks for it (`decisions.md`, 2026-11-12). More agents are split out only when two pieces of work really run in
parallel (one **apps-dev** per app, for example).

| Role | Definition | Owns | Hands back |
| --- | --- | --- | --- |
| Controller | the main session | backlog (`ROADMAP.md`), task briefs, priorities, hand-offs, the conversation | status to the user, decisions needed, release asks |
| `launcher-dev` | `.claude/agents/launcher-dev.md` | the launcher repo: generic capabilities, fixes, bundle, installer | a built binary, a change list, the test plan |
| `apps-dev` | `.claude/agents/apps-dev.md` | the apps repo: an app, its `.deb`, registry entry, licence notice | a package in `packages/`, the registry diff, the test plan |
| `deck-verifier` | `.claude/agents/deck-verifier.md` | on request only: the physical deck and the verification checklist | deploy results, screenshots, a pass/fail verification report |
| `docs-writer` | `.claude/agents/docs-writer.md` | READMEs, guides, credits, release notes | the updated docs and a link/credit check |

The builder of a change never signs it off. By default the owner's physical test result accepts a change, from the
Controller's written test protocol; no `deck-verifier` report is needed (owner decision of 2026-11-12, token cost). When
the owner asks for a `deck-verifier` run, its FAIL still goes back to the owner of the change.

## Lifecycle of a task

1. **Brief.** The Controller writes `docs/dev/tasks/NNN-name.md` (goal, owner, acceptance criteria, constraints, what
   it must not touch) and agrees it with the user.
2. **Build.** The owner works in its own git worktree and builds with the WSL lock (see the dev guide, "Build, test,
   deploy": `tools/dev/wsl-build.sh`, `tools/dev/check-all.sh`, the launcher UI definition of done). It does not
   commit to `main`: it leaves changes uncommitted or on a branch and writes a hand-off report.
3. **Deploy and observe.** The Controller (with `tools/boards/board.py`; `deck-verifier` only when the owner asks) installs the result on
   the boards (an app through Settings > Apps, a launcher binary by copying it) and captures screenshots and logs. The owner runs the
   physical tests from the Controller's written protocol.
4. **Verify.** The Controller checks the code and runs `tools/dev/check-all.sh`; the owner's test result is the sign-off. When
   `deck-verifier` is asked for, it runs the checklists in `docs/dev/checklists/` and writes `docs/dev/reports/NNN.md`.
   Failures go back to the owner of the change with the evidence.
5. **Docs.** `docs-writer` updates the READMEs, guides and credits for the change.
6. **User acceptance.** The Controller asks the user to try it on the deck, saying exactly what to look at.
7. **Commit, push, release** only on the user's explicit go-ahead (an approval gate). The Controller does the git
   work or delegates it with the exact commands.

## Hand-off report (every agent ends with this)

```
Task: NNN name        Agent: <role>        Result: done | blocked | needs decision
Changed: files / packages (paths)
How it was checked: commands run and their outcome, screenshots (paths)
Not done / risks:
Needs from the Controller: (decision, approval, another agent)
```

## Communication with the user

The Controller reports in this shape and nothing longer: what is done, what the user must try on the deck (one
short list), what is blocked, the decision needed. It never claims something works that was only built; it says
what was run and what was seen.

## When something does not fit

A request that needs a launcher change from an app task, a decision on licensing, a boot-config or system change,
or anything outward-facing goes to the Controller, which asks the user. Agents do not decide these themselves.
