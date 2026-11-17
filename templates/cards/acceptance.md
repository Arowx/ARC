# Acceptance: cards starter (Twenty-One)

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.
`stack` puts chosen cards on top of the deck, so every hand below is the same on every run.

```arc-check
game .
do step 2
expect game.phase == "bet"
expect game.chips == 100
expect game.bet == 10
expect frame.texts[*].text contains "CHIPS 100"
expect ui[label=Deal].kind == "button"
# the deal: player, dealer, player, dealer; the dealer's second card stays hidden
do stack 10H,6S,QD,9C; ui_click Deal; step 2
expect game.phase == "player"
expect game.player_value == 20
expect game.dealer[1] == "??"
expect game.dealer_value == 6
# stand: the dealer reveals and draws to 17 (6 + 9 + 2 = 17), 20 beats 17
do stack 2D; ui_click Stand; step 120
expect game.phase == "result"
expect game.dealer_value == 17
expect game.result == "win"
expect game.chips == 110
# going over 21 loses the bet at once
do stack 10H,6S,QD,9C,5H; deal; step 2; hit; step 2
expect game.result == "bust"
expect game.chips == 100
# a two-card 21 pays 3 to 2
do bet 20; stack AS,6S,KD,9C; deal; step 2
expect game.result == "blackjack"
expect game.chips == 130
# equal totals: the bet comes back
do bet 10; stack 10H,10S,8D,8C; deal; step 2; stand; step 120
expect game.result == "push"
expect game.chips == 130
# out of chips
do chips 10; stack 10H,6S,QD,9C,5H; deal; step 2; hit; step 2
expect game.phase == "broke"
```
