/* {title} - a card game: Twenty-One against the dealer (ARC template "cards").
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
 *   looks            cards fly from the shoe one after another and flip over as they land (dealt_tick), the hole
 *                    card turns when the dealer plays (reveal_tick), the bet's chips slide to the winner
 *                    (payout_tick), the result pops, "+20" / "-10" float up (arc_popup_text_2d). The card face is
 *                    drawn on a rounded card made at start, with the suit art in assets/textures/suits.png.
 *   assets/          felt, card back, suits, chip, sounds and music made with Ludo; assets/manifest.json says how.
 *   dump()           phase, chips, bet, both hands as text ("AS", "10H"), totals, the result.
 *   command()        agent commands: deal, hit, stand, bet N, chips N, stack AS,KH,... (a fresh deck, these on top), restart.
 * Grow it: other games on the same deck and hands (a solitaire, a trick-taking game: hands of 13, a table of
 * played cards), dragging cards (arc_mouse + arc_key_down(ARC_MOUSE_LEFT)), split and double down.
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
    int reveal_tick, payout_tick, payout_dir, payout_bet, result_tick;   /* looks: when the hole card turned, chips moved, the result showed */
    int deck[52], top;            /* deck[top..51] are still to draw */
    Hand player, dealer;
} Game;
static Game G;

static const char *RANKS[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
static const char SUITS[4] = { 'S', 'H', 'D', 'C' };
static arc_sound s_card, s_flip, s_chips, s_win, s_lose, s_shuffle;
static arc_music music;
static arc_texture t_felt, t_back, t_face, t_suits, t_chip;
#define CW 90.0f              /* a card on screen */
#define CH 126.0f
#define DEAL_GAP 9            /* ticks between cards in a deal */
#define FLY 14                /* ticks a card flies */

static void card_name(int c, char *out) { snprintf(out, 4, "%s%c", RANKS[c % 13], SUITS[c / 13]); }
static int parse_card(const char *s, int len) {   /* "AS", "10h", "kd" (len characters) -> 0..51, or -1 */
    if (len < 2 || len > 3) return -1;
    char suit = s[len - 1] & ~32, rank[3] = { 0 };
    for (int i = 0; i < len - 1; i++) rank[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i];
    for (int r = 0; r < 13; r++) if (!strcmp(rank, RANKS[r])) for (int u = 0; u < 4; u++) if (suit == SUITS[u]) return u * 13 + r;
    return -1;
}
static void shuffle(void) {
    if (G.hands_played > 0 || G.top > 0) arc_play(s_shuffle, 0.5f, 0);
    for (int i = 0; i < 52; i++) G.deck[i] = i;
    for (int i = 51; i > 0; i--) { int j = arc_rand_range(0, i); int t = G.deck[i]; G.deck[i] = G.deck[j]; G.deck[j] = t; }
    G.top = 0;
}
static int draw_card(void) { if (G.top >= 52) shuffle(); return G.deck[G.top++]; }
static int deal_queue;   /* looks: cards given in one go leave the shoe one after another */
static void give(Hand *h) { if (h->n < MAX_HAND) { h->dealt_tick[h->n] = (int)arc_tick_count() + DEAL_GAP * deal_queue++; h->c[h->n++] = draw_card(); } }
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
    int last = 0; for (int i = 0; i < G.player.n; i++) if (G.player.dealt_tick[i] > last) last = G.player.dealt_tick[i];
    for (int i = 0; i < G.dealer.n; i++) if (G.dealer.dealt_tick[i] > last) last = G.dealer.dealt_tick[i];
    G.result_tick = (last > (int)arc_tick_count() ? last : (int)arc_tick_count()) + FLY + 6;   /* after the last card lands */
    G.payout_tick = G.result_tick + 20; G.payout_bet = G.bet;
    G.payout_dir = result == RES_WIN || result == RES_BLACKJACK ? 1 : result == RES_PUSH ? 0 : -1;
    if (result == RES_WIN) G.chips += G.bet; else if (result == RES_BLACKJACK) G.chips += G.bet * 3 / 2;
    else if (result == RES_LOSE || result == RES_BUST) G.chips -= G.bet;
    arc_event("hand", "\"result\":%d,\"chips\":%d", result, G.chips);
    if (G.chips <= 0) G.phase = PHASE_BROKE;
    else if (G.bet > G.chips) G.bet = G.chips;
}
static void deal(void) {
    if (G.phase != PHASE_BET && G.phase != PHASE_RESULT) return;
    if (G.top > 52 - 15) shuffle();
    memset(&G.player, 0, sizeof G.player); memset(&G.dealer, 0, sizeof G.dealer); G.result = RES_NONE;
    arc_play_var(s_chips, 0.5f, 0, 0.1f);
    deal_queue = 0; give(&G.player); give(&G.dealer); give(&G.player); give(&G.dealer); deal_queue = 0;
    G.phase = PHASE_PLAYER;
    if (value(&G.player) == 21) settle(value(&G.dealer) == 21 ? RES_PUSH : RES_BLACKJACK);
}
static void hit(void) { if (G.phase != PHASE_PLAYER) return; deal_queue = 0; give(&G.player); if (value(&G.player) > 21) settle(RES_BUST); else if (value(&G.player) == 21) { G.phase = PHASE_DEALER; G.dealer_timer = 30; G.reveal_tick = (int)arc_tick_count(); } }
static void stand(void) { if (G.phase == PHASE_PLAYER) { G.phase = PHASE_DEALER; G.dealer_timer = 30; G.reveal_tick = (int)arc_tick_count(); } }
static void reset(void) { memset(&G, 0, sizeof G); G.chips = 100; G.bet = 10; shuffle(); }

static void init(void) {
    s_card = arc_sound_load("assets/sfx/deal.mp3"); s_flip = arc_sound_load("assets/sfx/flip.mp3");
    s_chips = arc_sound_load("assets/sfx/chips.mp3"); s_win = arc_sound_load("assets/sfx/win.mp3");
    s_lose = arc_sound_load("assets/sfx/lose.mp3"); s_shuffle = arc_sound_load("assets/sfx/shuffle.mp3");
    music = arc_music_load("assets/music/music.mp3"); arc_music_set_volume(music, 0.35f); arc_music_play(music, true);
    t_felt = arc_texture_load_ex("assets/textures/felt.jpg", ARC_TEX_SMOOTH | ARC_TEX_REPEAT);
    t_back = arc_texture_load_ex("assets/textures/card_back.png", ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS);
    t_suits = arc_texture_load_ex("assets/textures/suits.png", ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS);
    t_chip = arc_texture_load_ex("assets/textures/chip.png", ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS);
    static uint8_t px[180 * 252 * 4];   /* the card face: a rounded white card with a faint edge */
    for (int y = 0; y < 252; y++) for (int x = 0; x < 180; x++) {
        float r = 16, dx = fmaxf(fmaxf(r - (float)x, (float)x - (179 - r)), 0), dy = fmaxf(fmaxf(r - (float)y, (float)y - (251 - r)), 0);
        float d = sqrtf(dx * dx + dy * dy), a = arc_clampf(r + 0.5f - d, 0, 1), edge = arc_clampf(d - (r - 3), 0, 1);
        uint8_t *q = &px[(y * 180 + x) * 4]; uint8_t c = (uint8_t)(250 - 40 * edge);
        q[0] = c; q[1] = c; q[2] = (uint8_t)(c - 6); q[3] = (uint8_t)(a * 255);
    }
    t_face = arc_texture_create(180, 252, px, true);
    reset();
}
static void tick(void) {
    float W = (float)arc_width(), H = (float)arc_height();
    arc_ui_begin("", W - 230, H - 210, 220);
    arc_ui_labelf("Bet: %d", G.bet);
    arc_ui_columns(2);
    bool betting = G.phase == PHASE_BET || G.phase == PHASE_RESULT;
    if (arc_ui_button_ex("Bet -10", betting && G.bet > 10)) { G.bet -= 10; arc_play_var(s_chips, 0.4f, 0, 0.15f); }
    if (arc_ui_button_ex("Bet +10", betting && G.bet + 10 <= G.chips)) { G.bet += 10; arc_play_var(s_chips, 0.4f, 0, 0.15f); }
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
    int now = (int)arc_tick_count();   /* looks: sounds when cards leave the shoe and flip, chips when they move */
    const Hand *hs[2] = { &G.player, &G.dealer };
    for (int k = 0; k < 2; k++) for (int i = 0; i < hs[k]->n; i++) {
        if (hs[k]->dealt_tick[i] == now) arc_play_var(s_card, 0.5f, 0, 0.1f);
        if (hs[k]->dealt_tick[i] + FLY == now && !(k == 1 && i == 1 && dealer_hidden())) arc_play_var(s_flip, 0.35f, 0, 0.1f);
    }
    if (G.reveal_tick && G.reveal_tick == now) arc_play_var(s_flip, 0.5f, 0, 0.05f);
    if (G.result_tick && G.result_tick == now)
        arc_play(G.result == RES_WIN || G.result == RES_BLACKJACK ? s_win : G.result == RES_PUSH ? s_chips : s_lose, 0.5f, 0);
    if (G.payout_tick && G.payout_tick == now) {
        arc_play_var(s_chips, 0.6f, 0, 0.1f);
        int d = G.payout_dir > 0 ? (G.result == RES_BLACKJACK ? G.payout_bet * 3 / 2 : G.payout_bet) : G.payout_dir < 0 ? -G.payout_bet : 0;
        arc_popup_text_2d(90, (float)arc_height() - 170, d > 0 ? 0x60FF80 : d < 0 ? 0xFF7060 : 0xFFFFFF, 3, d ? arc_fmt("%+d", d) : "PUSH");
    }
    if (G.phase == PHASE_DEALER && --G.dealer_timer <= 0) {   /* the dealer draws to 17, one card at a time */
        if (value(&G.dealer) < 17) { deal_queue = 0; give(&G.dealer); G.dealer_timer = 30; }
        else {
            int p = value(&G.player), d = value(&G.dealer);
            settle(d > 21 || p > d ? RES_WIN : p < d ? RES_LOSE : RES_PUSH);
        }
    }
}
static void draw_suit(int suit, float cx, float cy, float size) {
    arc_draw_texture_ex(t_suits, (float)suit * 128, 0, 128, 128, cx, cy, size, size, size / 2, size / 2, 0, 0xFFFFFF);
}
/* one card: back or face, centred at (cx, cy), squeezed to `open` of its width (a flip) and turned by `rot` */
static void draw_card_img(float cx, float cy, int c, bool face, float open, float rot) {
    float w = CW * open;
    arc_draw_texture_ex(t_face, 0, 0, 180, 252, cx + 5, cy + 7, w, CH, w / 2, CH / 2, rot, arc_rgba(0x000000, 0.35f));   /* shadow */
    if (!face) { arc_draw_texture_ex(t_back, 0, 0, 240, 336, cx, cy, w, CH, w / 2, CH / 2, rot, 0xFFFFFF); return; }
    arc_draw_texture_ex(t_face, 0, 0, 180, 252, cx, cy, w, CH, w / 2, CH / 2, rot, 0xFFFFFF);
    if (open < 0.6f || fabsf(rot) > 0.05f) return;   /* the markings only on a card that lies square */
    int suit = c / 13; uint32_t col = suit == 1 || suit == 2 ? 0xD02020 : 0x202020;
    float x0 = cx - w / 2, y0 = cy - CH / 2;
    arc_draw_text_ex(x0 + 8, y0 + 7, 2.5f, col, 0, RANKS[c % 13]);
    draw_suit(suit, x0 + 15, y0 + 38, 18 * open);
    arc_draw_text_ex(x0 + w - 8, y0 + CH - 7, 2.5f, col, ARC_TEXT_RIGHT | ARC_TEXT_BOTTOM, RANKS[c % 13]);
    draw_suit(suit, cx, cy + 6, 52 * open);
}
static void draw_hand(const Hand *h, float y, bool hide_second) {
    float W = (float)arc_width(), x0 = W / 2 - ((float)h->n * (CW + 12) - 12) / 2 + CW / 2;
    int now = (int)arc_tick_count();
    for (int i = 0; i < h->n; i++) {
        int t = now - h->dealt_tick[i];
        if (t < 0) continue;   /* still in the shoe */
        float f = arc_clampf((float)t / (float)FLY, 0, 1), e = arc_ease(ARC_EASE_OUT_CUBIC, f);
        float x = arc_lerpf(W - 95, x0 + (float)i * (CW + 12), e), yy = arc_lerpf(110, y, e) - 40 * sinf(f * 3.14159f) * (1 - f);
        float rot = (1 - e) * -0.6f;
        /* the flip: after landing the card closes edge-on and opens with its face up (the hole card waits for the reveal) */
        bool hole = hide_second && i == 1;
        int flip_at = h == &G.dealer && i == 1 && G.reveal_tick ? G.reveal_tick : h->dealt_tick[i] + FLY;
        if (hole) flip_at = 1 << 30;
        float ft = (float)(now - flip_at) / 8.0f;
        bool face = ft >= 0.5f; float open = ft < 0 ? 1 : ft < 0.5f ? 1 - ft * 2 : ft < 1 ? (ft - 0.5f) * 2 : 1;
        draw_card_img(x, yy + CH / 2, h->c[i], face, open, rot);
    }
}
static void draw_chips(float x, float y, int count, float lift) {   /* a stack: one chip per 10 */
    for (int i = 0; i < count && i < 24; i++)
        arc_draw_texture_ex(t_chip, 0, 0, 128, 128, x + (float)((i * 7) % 3) - 1, y - (float)i * 4 - lift, 44, 44, 22, 22, 0, 0xFFFFFF);
}
static void draw(float alpha) {
    (void)alpha;
    float W = (float)arc_width(), H = (float)arc_height();
    int now = (int)arc_tick_count();
    arc_draw_sprite(t_felt, 0, 0, W, H, 0, 0, W / 256, H / 256, 0xFFFFFF);   /* the felt, tiled */
    arc_draw_rect(0, 0, W, 10, arc_rgba(0x2A1808, 0.9f)); arc_draw_rect(0, H - 10, W, 10, arc_rgba(0x2A1808, 0.9f));   /* the rail */
    for (int i = 0; i < 4; i++) draw_card_img(W - 95 + (float)i * 1.5f, 110 + CH / 2 - (float)i * 1.5f, 0, false, 1, 0);   /* the shoe */
    arc_draw_text_ex(W - 95, 110 + CH + 12, 1.5f, 0xC0E0C8, ARC_TEXT_CENTER, arc_fmt("%d left", 52 - G.top));
    bool hidden = dealer_hidden();
    arc_draw_text_ex(20, 20, 3, 0xFFF0A0, ARC_TEXT_OUTLINE, arc_fmt("CHIPS %d", G.chips));
    draw_chips(60, H - 90, G.chips / 10 > 12 ? 12 : G.chips / 10, 0);   /* your chips, by your hand */
    if (G.chips / 10 > 12) draw_chips(110, H - 90, G.chips / 10 - 12 > 12 ? 12 : G.chips / 10 - 12, 0);
    arc_draw_text_ex(W / 2, 64, 2, 0xC0E0C8, ARC_TEXT_CENTER, G.dealer.n ? (hidden ? arc_fmt("Dealer %d + ?", hand_value(&G.dealer, 1)) : arc_fmt("Dealer %d", value(&G.dealer))) : "Dealer");
    draw_hand(&G.dealer, 90, hidden);
    arc_draw_text_ex(W / 2, H - 70, 2, 0xC0E0C8, ARC_TEXT_CENTER, G.player.n ? arc_fmt("You %d", value(&G.player)) : "You");
    draw_hand(&G.player, H - 250, false);
    /* the bet: a stack in the middle, which slides to the winner when the hand is settled */
    float bx = W / 2 - 160, by = H / 2 + 30;
    int pt = G.payout_tick ? now - G.payout_tick : -1;
    if (G.phase == PHASE_RESULT && pt >= 0 && pt < 30) {
        float e = arc_ease(ARC_EASE_INOUT_CUBIC, (float)pt / 30.0f);
        float tx = G.payout_dir > 0 ? 60 : G.payout_dir < 0 ? W / 2 : bx, ty = G.payout_dir > 0 ? H - 90 : G.payout_dir < 0 ? 60 : by;
        draw_chips(arc_lerpf(bx, tx, e), arc_lerpf(by, ty, e), G.payout_bet / 10, 0);
        if (G.payout_dir > 0) draw_chips(arc_lerpf(W / 2, tx, e), arc_lerpf(60, ty, e), G.payout_bet / 10, 0);   /* the dealer pays */
    } else if (G.phase != PHASE_RESULT || pt < 0) draw_chips(bx, by, G.bet / 10, 0);
    arc_draw_text_ex(bx, by + 30, 2, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, arc_fmt("BET %d", G.bet));
    static const char *msg[] = { "", "YOU WIN", "DEALER WINS", "PUSH", "TWENTY-ONE! 3 TO 2", "BUST" };
    static const uint32_t mcol[] = { 0xFFF0A0, 0x70FF90, 0xFF7060, 0xFFFFFF, 0xFFE040, 0xFF7060 };
    bool shown = G.phase != PHASE_RESULT && G.phase != PHASE_BROKE ? true : now >= G.result_tick;
    const char *m = G.phase == PHASE_BET ? "Place your bet and Deal (SPACE)" : G.phase == PHASE_PLAYER ? "Hit (H) or Stand (S)"
                  : G.phase == PHASE_DEALER ? "Dealer plays..." : G.phase == PHASE_BROKE ? "OUT OF CHIPS - R for a new game" : msg[G.result];
    float sc = 3;
    if ((G.phase == PHASE_RESULT || G.phase == PHASE_BROKE) && shown) sc = 3 + 1.5f * arc_ease_out_back(arc_clampf((float)(now - G.result_tick) / 15.0f, 0, 1));
    if (shown) arc_draw_text_ex(W / 2, H / 2 - 20, sc, G.phase == PHASE_RESULT ? mcol[G.result] : 0xFFF0A0, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, m);
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
    return (arc_app){ .title = "{title}", .width = 960, .height = 640, .clear_color = 0x14401E,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
