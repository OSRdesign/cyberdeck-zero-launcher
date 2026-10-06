# Roles and workflow

Lean setup: one **Controller** (the main Claude Code session, the only one who talks to the user) and four
subagents defined in `.claude/agents/`. More agents are split out only when two pieces of work really run in
parallel (one **apps-dev** per app, for example).

| Role | Definition | Owns | Hands back |
| --- | --- | --- | --- |
| Controller | the main session | backlog (`ROADMAP.md`), task briefs, priorities, hand-offs, the conversation | status to the user, decisions needed, release asks |
| `launcher-dev` | `.claude/agents/launcher-dev.md` | the launcher repo: generic capabilities, fixes, bundle, installer | a built binary, a change list, the test plan |
| `apps-dev` | `.claude/agents/apps-dev.md` | the apps repo: an app, its `.deb`, registry entry, licence notice | a package in `packages/`, the registry diff, the test plan |
| `deck-verifier` | `.claude/agents/deck-verifier.md` | the physical deck **and** the quality gate | deploy results, screenshots, a pass/fail verification report |
| `docs-writer` | `.claude/agents/docs-writer.md` | READMEs, guides, credits, release notes | the updated docs and a link/credit check |
| `x86-architect` | `.claude/agents/x86-architect.md` | the design of the x86 port (target: Panasonic FZ-M1, Atom x5, Armbian x86 / Ubuntu 26.04) | a port report with options, pros/cons, token cost and a recommendation; no code |

The builder of a change never signs it off: `deck-verifier` does, and it has a veto on any release.

## Lifecycle of a task

1. **Brief.** The Controller writes `docs/dev/tasks/NNN-name.md` (goal, owner, acceptance criteria, constraints, what
   it must not touch) and agrees it with the user.
2. **Build.** The owner works in its own git worktree and builds with the WSL lock (see the dev guide). It does not
   commit to `main`: it leaves changes uncommitted or on a branch and writes a hand-off report.
3. **Deploy and observe.** `deck-verifier` installs the result on the deck (an app through Settings > Apps, a
   launcher binary by copying it) and captures screenshots and logs.
4. **Verify.** `deck-verifier` runs the checklists in `docs/dev/checklists/` and writes `docs/dev/reports/NNN.md`.
   Failures go back to the owner with the evidence.
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
