/* {title} - a card game starter: Twenty-One against the dealer (ARC template "cards").
 *
 * Bet, then Deal (SPACE). Hit (H) for another card, Stand (S) to let the dealer play: the dealer draws to 17 and
 * stands. Closest to 21 without going over wins the bet; a two-card 21 pays 3 to 2; equal totals give the bet back.
 * Aces count 1 or 11. The deck is reshuffled when it runs low. Start with 100 chips; at 0, R starts again.
 *
 * Where things are:
 *   the deck         52 cards as numbers (rank = c % 13, suit = c / 13), shuffled with arc_rand (seeded by --seed,
 *                    so the same seed deals the same cards); draw_card() takes the top one.
 *   Hand             up to 12 cards; hand_value() counts aces as 11 while that stays at 21 or below.
 *   Game G           everything, pointer-free: save_state/load_state copy it (rewind, seek and the timeline work).
 *   tick()           buttons and keys, the dealer's turn (one card every 30 ticks, so you can watch), payouts.
 *   draw_card_face() a card from rects and text: replace it with sprites (arc_draw_sprite) when you have art.
 *   dump()           phase, chips, bet, both hands as text ("AS", "10H"), totals, the result.
 *   command()        agent commands: deal, hit, stand, bet N, chips N, stack AS,KH,... (a fresh deck, these on top), restart.
 * Grow it: other games on the same deck and hands (a solitaire, a trick-taking game: hands of 13, a table of
 * played cards), dragging cards (arc_mouse + arc_key_down(ARC_MOUSE_LEFT)), card art (a 13x4 sprite sheet).
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_HAND 12
enum { PHASE_BET, PHASE_PLAYER, PHASE_DEALER, PHASE_RESULT, PHASE_BROKE };
enum { RES_NONE, RES_WIN, RES_LOSE, RES_PUSH, RES_BLACKJACK, RES_BUST };
typedef struct { int n; int c[MAX_HAND]; int dealt_tick[MAX_HAND]; } Hand;
typedef struct {
    int phase, chips, bet, result, hands_played, dealer_timer;
    int deck[52], top;            /* deck[top..51] are still to draw */
    Hand player, dealer;
} Game;
static Game G;

static const char *RANKS[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
static const char SUITS[4] = { 'S', 'H', 'D', 'C' };
static arc_sound s_card, s_win, s_lose;

static void card_name(int c, char *out) { snprintf(out, 4, "%s%c", RANKS[c % 13], SUITS[c / 13]); }
static int parse_card(const char *s, int len) {   /* "AS", "10h", "kd" (len characters) -> 0..51, or -1 */
    if (len < 2 || len > 3) return -1;
    char suit = s[len - 1] & ~32, rank[3] = { 0 };
    for (int i = 0; i < len - 1; i++) rank[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i];
    for (int r = 0; r < 13; r++) if (!strcmp(rank, RANKS[r])) for (int u = 0; u < 4; u++) if (suit == SUITS[u]) return u * 13 + r;
    return -1;
}
static void shuffle(void) {
    for (int i = 0; i < 52; i++) G.deck[i] = i;
    for (int i = 51; i > 0; i--) { int j = arc_rand_range(0, i); int t = G.deck[i]; G.deck[i] = G.deck[j]; G.deck[j] = t; }
    G.top = 0;
}
static int draw_card(void) { if (G.top >= 52) shuffle(); return G.deck[G.top++]; }
static void give(Hand *h) { if (h->n < MAX_HAND) { h->dealt_tick[h->n] = (int)arc_tick_count(); h->c[h->n++] = draw_card(); arc_play(s_card, 0.3f, 0); } }
static int hand_value(const Hand *h, int count) {   /* first `count` cards */
    int v = 0, aces = 0;
    for (int i = 0; i < count && i < h->n; i++) { int r = h->c[i] % 13; v += r == 0 ? 1 : r >= 9 ? 10 : r + 1; aces += r == 0; }
    if (aces && v + 10 <= 21) v += 10;
    return v;
}
static int value(const Hand *h) { return hand_value(h, h->n); }
static bool dealer_hidden(void) { return G.phase == PHASE_PLAYER; }   /* the second dealer card is face down */

static void settle(int result) {
    G.result = result; G.phase = PHASE_RESULT; G.hands_played++;
    if (result == RES_WIN) G.chips += G.bet; else if (result == RES_BLACKJACK) G.chips += G.bet * 3 / 2;
    else if (result == RES_LOSE || result == RES_BUST) G.chips -= G.bet;
    arc_play(result == RES_WIN || result == RES_BLACKJACK ? s_win : s_lose, 0.4f, 0);
    arc_event("hand", "\"result\":%d,\"chips\":%d", result, G.chips);
    if (G.chips <= 0) G.phase = PHASE_BROKE;
    else if (G.bet > G.chips) G.bet = G.chips;
}
static void deal(void) {
    if (G.phase != PHASE_BET && G.phase != PHASE_RESULT) return;
    if (G.top > 52 - 15) shuffle();
    memset(&G.player, 0, sizeof G.player); memset(&G.dealer, 0, sizeof G.dealer); G.result = RES_NONE;
    give(&G.player); give(&G.dealer); give(&G.player); give(&G.dealer);
    G.phase = PHASE_PLAYER;
    if (value(&G.player) == 21) settle(value(&G.dealer) == 21 ? RES_PUSH : RES_BLACKJACK);
}
static void hit(void) { if (G.phase != PHASE_PLAYER) return; give(&G.player); if (value(&G.player) > 21) settle(RES_BUST); else if (value(&G.player) == 21) { G.phase = PHASE_DEALER; G.dealer_timer = 30; } }
static void stand(void) { if (G.phase == PHASE_PLAYER) { G.phase = PHASE_DEALER; G.dealer_timer = 30; } }
static void reset(void) { memset(&G, 0, sizeof G); G.chips = 100; G.bet = 10; shuffle(); }

static void init(void) {
    s_card = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_NOISE, .freq = 3000, .freq_end = 1500, .sustain = 0.01f, .decay = 0.05f });
    s_win = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_TRIANGLE, .freq = 660, .freq_end = 1320, .sustain = 0.15f, .decay = 0.3f });
    s_lose = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SAW, .freq = 300, .freq_end = 150, .sustain = 0.1f, .decay = 0.3f });
    reset();
}
static void tick(void) {
    float W = (float)arc_width(), H = (float)arc_height();
    arc_ui_begin("", W - 230, H - 210, 220);
    arc_ui_labelf("Bet: %d", G.bet);
    arc_ui_columns(2);
    bool betting = G.phase == PHASE_BET || G.phase == PHASE_RESULT;
    if (arc_ui_button_ex("Bet -10", betting && G.bet > 10)) G.bet -= 10;
    if (arc_ui_button_ex("Bet +10", betting && G.bet + 10 <= G.chips)) G.bet += 10;
    arc_ui_columns(1);
    if (arc_ui_button_ex("Deal", betting)) deal();
    arc_ui_columns(2);
    if (arc_ui_button_ex("Hit", G.phase == PHASE_PLAYER)) hit();
    if (arc_ui_button_ex("Stand", G.phase == PHASE_PLAYER)) stand();
    arc_ui_columns(1);
    if (G.phase == PHASE_BROKE && arc_ui_button("New game")) reset();
    arc_ui_end();
    if (!arc_ui_wants_keyboard()) {
        if (arc_key_pressed(ARC_KEY_SPACE)) deal();
        if (arc_key_pressed(ARC_KEY_H)) hit();
        if (arc_key_pressed(ARC_KEY_S)) stand();
        if (arc_key_pressed(ARC_KEY_R) && G.phase == PHASE_BROKE) reset();
    }
    if (G.phase == PHASE_DEALER && --G.dealer_timer <= 0) {   /* the dealer draws to 17, one card at a time */
        if (value(&G.dealer) < 17) { give(&G.dealer); G.dealer_timer = 30; }
        else {
            int p = value(&G.player), d = value(&G.dealer);
            settle(d > 21 || p > d ? RES_WIN : p < d ? RES_LOSE : RES_PUSH);
        }
    }
}
static void draw_card_face(float x, float y, int c, bool face_down) {
    arc_draw_rect(x + 4, y + 5, 80, 112, 0x60000000);   /* shadow */
    if (face_down) {
        arc_draw_rect(x, y, 80, 112, 0x2850A0); arc_draw_rect_lines(x + 5, y + 5, 70, 102, 0xA0C0F0);
        return;
    }
    arc_draw_rect(x, y, 80, 112, 0xF8F8F2); arc_draw_rect_lines(x, y, 80, 112, 0x303030);
    char nm[4]; card_name(c, nm);
    uint32_t col = (c / 13 == 1 || c / 13 == 2) ? 0xD02020 : 0x202020;
    arc_draw_text(x + 6, y + 6, 2, col, nm);
    arc_draw_text_ex(x + 40, y + 56, 4, col, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE, RANKS[c % 13]);
}
static void draw_hand(const Hand *h, float y, bool hide_second) {
    float W = (float)arc_width(), x0 = W / 2 - ((float)(h->n * 90 - 10)) / 2.0f;
    for (int i = 0; i < h->n; i++) {
        float t = arc_clampf(((float)((int)arc_tick_count() - h->dealt_tick[i])) / 12.0f, 0, 1);   /* slide in from the deck */
        float e = arc_ease(ARC_EASE_OUT_CUBIC, t);
        float x = arc_lerpf(W - 120, x0 + (float)(i * 90), e), yy = arc_lerpf(40, y, e);
        draw_card_face(x, yy, h->c[i], hide_second && i == 1);
    }
}
static void draw(float alpha) {
    (void)alpha;
    float W = (float)arc_width(), H = (float)arc_height();
    arc_draw_rect(0, 0, W, H, 0x1E5A34);
    arc_draw_rect(W - 120, 40, 80, 112, 0x2850A0); arc_draw_rect_lines(W - 115, 45, 70, 102, 0xA0C0F0);   /* the deck */
    arc_draw_textf(W - 120, 160, 1, 0xC0E0C8, "%d left", 52 - G.top);
    bool hidden = dealer_hidden();
    arc_draw_textf(20, 20, 3, 0xFFFFFF, "CHIPS %d", G.chips);
    arc_draw_textf(20, 54, 2, 0xC0E0C8, "Dealer %s", G.dealer.n ? (hidden ? arc_fmt("%d + ?", hand_value(&G.dealer, 1)) : arc_fmt("%d", value(&G.dealer))) : "");
    draw_hand(&G.dealer, 90, hidden);
    arc_draw_textf(20, H - 60, 2, 0xC0E0C8, "You %s", G.player.n ? arc_fmt("%d", value(&G.player)) : "");
    draw_hand(&G.player, H - 230, false);
    static const char *msg[] = { "", "YOU WIN", "DEALER WINS", "PUSH", "TWENTY-ONE! 3 TO 2", "BUST" };
    const char *m = G.phase == PHASE_BET ? "Place your bet and Deal (SPACE)" : G.phase == PHASE_PLAYER ? "Hit (H) or Stand (S)"
                  : G.phase == PHASE_DEALER ? "Dealer plays..." : G.phase == PHASE_BROKE ? "OUT OF CHIPS - R for a new game" : msg[G.result];
    arc_draw_text_ex(W / 2, H / 2 - 20, 3, 0xFFF0A0, ARC_TEXT_CENTER | ARC_TEXT_SHADOW, m);
}
static void hand_json(arc_json *j, const char *key, const Hand *h, bool hide_second) {
    arc_json_arr_k(j, key);
    for (int i = 0; i < h->n; i++) { char nm[4]; card_name(h->c[i], nm); arc_json_str(j, hide_second && i == 1 ? "??" : nm); }
    arc_json_end(j);
}
static void dump(arc_json *j) {
    static const char *ph[] = { "bet", "player", "dealer", "result", "broke" };
    static const char *res[] = { "", "win", "lose", "push", "blackjack", "bust" };
    bool hidden = dealer_hidden();
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "chips", G.chips); arc_json_num_k(j, "bet", G.bet);
    arc_json_str_k(j, "result", res[G.result]); arc_json_num_k(j, "hands_played", G.hands_played); arc_json_num_k(j, "deck_left", 52 - G.top);
    hand_json(j, "player", &G.player, false); arc_json_num_k(j, "player_value", value(&G.player));
    hand_json(j, "dealer", &G.dealer, hidden); arc_json_num_k(j, "dealer_value", hidden ? hand_value(&G.dealer, 1) : value(&G.dealer));
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "deal")) { deal(); return true; }
    if (!strcmp(name, "hit")) { hit(); return true; }
    if (!strcmp(name, "stand")) { stand(); return true; }
    if (!strcmp(name, "bet") && argc >= 1) { int b = atoi(argv[0]); if (b > 0 && b <= G.chips) G.bet = b; return true; }
    if (!strcmp(name, "chips") && argc >= 1) { G.chips = atoi(argv[0]); return true; }
    if (!strcmp(name, "stack") && argc >= 1) {   /* stack AS,KH,10D: a fresh deck with these on top, in this order */
        shuffle();
        int at = 0;
        for (const char *t = argv[0]; *t && at < 52; ) {
            int len = 0; while (t[len] && t[len] != ',') len++;
            int c = parse_card(t, len);
            if (c < 0) arc_log("stack: not a card: %.*s", len, t);
            else for (int k = at; k < 52; k++) if (G.deck[k] == c) { G.deck[k] = G.deck[at]; G.deck[at++] = c; break; }
            t += len; if (*t == ',') t++;
        }
        return true;
    }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 640, .clear_color = 0x1E5A34,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
