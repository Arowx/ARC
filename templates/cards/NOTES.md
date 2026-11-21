## This game started from the cards template (Twenty-One)

- **Cards:** numbers 0..51 (`rank = c % 13`, `suit = c / 13`); `card_name()` gives "AS", "10H". The deck is
  `deck[top..51]`, shuffled with `arc_rand_range` (seeded by `--seed`); `draw_card()` takes the top one.
- **Hands:** `Hand` holds up to 12 cards and the tick each was dealt.
- **Flow:** `PHASE_BET` -> `deal()` -> `PHASE_PLAYER` (hit/stand) -> `PHASE_DEALER` (one card every 30 ticks, to 17) ->
  `settle()` -> `PHASE_RESULT` (deal again) or `PHASE_BROKE`.
- **Looks:** everything is drawn from ticks the rules recorded, so drawing changes nothing:
  - cards fly from the shoe in turn (`DEAL_GAP`, `FLY`) and flip as they land;
  - the hole card turns when the dealer plays (`reveal_tick`);
  - the bet's chips slide to the winner (`payout_tick`);
  - the result pops (`result_tick`), and "+20" / "-10" float up (`arc_popup_text_2d`).
  The card face is generated at start: a rounded card with the suit art from `assets/textures/suits.png` (S, H, D, C
  in 128 px cells).
- **Assets:** made with Ludo (ludo.ai). `assets/manifest.json` lists every file, how it was made and a description.
- **State:** `Game G` is pointer-free; `save_state`/`load_state` copy it whole.
- **Agent commands:** `deal`, `hit`, `stand`, `bet N`, `chips N`, `stack AS,KH,...` (a fresh deck with these cards on top),
  `restart`. The buttons work with `ui_click Deal` / `Hit` / `Stand` too.
- **Tests:** `tests/acceptance.md`.
