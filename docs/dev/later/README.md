# Later exploration

Work that was researched but is not scheduled. Nothing here is built or promised; read it before restarting the topic.

| Topic | Folder | State |
| --- | --- | --- |
| x86 port for the Panasonic FZ-M1 (Atom x5, 2 GB, Ubuntu 26.04) | `x86-fz-m1/` | Port report by the `x86-architect` agent (2026-10-07), tablet facts verified over ssh. Nine decisions waiting for the user (listed at the end of the report). Parked by the user: no brief, no branch, no code. |

`x86-fz-m1/` also holds the agent's probe script (`fzm1_probe.py`, reads the login from `FZM1_HOST` / `FZM1_USER` /
`FZM1_PW`), the raw tablet output (`fzm1_probe.txt`, password masked) and its scratch-build setup (`setup.sh`).
The agent definition is `.claude/agents/x86-architect.md` (a copy in `docs/dev/agents/`); it is idle until the topic restarts.
