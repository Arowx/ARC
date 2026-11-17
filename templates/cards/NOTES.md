## This game started from the cards template (Twenty-One)

- Cards are numbers 0..51 (`rank = c % 13`, `suit = c / 13`); `card_name()` gives "AS", "10H". The deck is
  `deck[top..51]`, shuffled with `arc_rand_range` (seeded by `--seed`); `draw_card()` takes the top one.
- `Hand` holds up to 12 cards and the tick each was dealt (the slide-in is drawn from that; drawing changes nothing).
- Flow: `PHASE_BET` -> `deal()` -> `PHASE_PLAYER` (hit/stand) -> `PHASE_DEALER` (one card every 30 ticks, to 17) ->
  `settle()` -> `PHASE_RESULT` (deal again) or `PHASE_BROKE`.
- State: `Game G` is pointer-free; `save_state`/`load_state` copy it whole.
- Agent commands: `deal`, `hit`, `stand`, `bet N`, `chips N`, `stack AS,KH,...` (a fresh deck with these cards on top), `restart`.
  The buttons work with `ui_click Deal` / `Hit` / `Stand` too.
- Tests: `tests/acceptance.md`.
