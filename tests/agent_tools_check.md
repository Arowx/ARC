# ARC 0.10.16 (tooling request #12): the sample the agent tools are tried on still passes its own check

`tests/agent_tools_test.py` runs `tools/arc_brief.py` and `tools/arc_report.py` on a new tactics game and on this
sample; `tests/mcp_test.py` runs `arc_run` (build, script and checks in one call).

```arc-check
game samples/board/board.c
do step 60
expect tick >= 60
```
