# Request (test fixture): ground markers and a tactics board (0.9.9)

**Why.** This file checks `samples/board` the way MechBattleArc's ground-marker request asked: markers are listed in
`frame.markers`, labels in `frame.texts`, and `arc_board` drives reach, paths and line of sight.

**Ask.** Nothing; the sample already does this.

## Acceptance

```arc-check
game samples/board/board.c
do step 5
expect count(frame.markers[*]) >= 3
expect count(frame.markers[shape=ring]) == 3
expect frame.markers[shape=chevron] exists
expect frame.texts[*].text contains "DROP ZONE"
expect game.reach > 20
expect count(game.sees[*]) >= 1
do move 7 6; step 2
expect game.x == 7
expect game.y == 6
do select 1; step 2
expect game.selected == 1
```
