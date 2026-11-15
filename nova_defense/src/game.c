/*
 * Nova Defense (Atari Lynx) - game logic, from
 * amiga_mcp/examples/nova_defense/game.c (by way of the Atari 8-bit port's
 * speed-ups). The rules, scoring, the march speed-up, firing rates, the
 * comet, shield erosion, waves and lives are the original's, in the same
 * order. Port changes are marked "port:".
 */
#pragma static-locals (on)
#include <string.h>
#include "lynxrnd.h"
#include "game.h"
#include "art.h"

uint8_t alive_mask[ALIEN_ROWS];
int16_t grid_x, grid_y;
int8_t swarm_dir;
uint8_t move_timer, move_delay, anim_frame, alive_count, fire_timer, fire_delay;
int16_t player_x;
int8_t lives;
uint32_t score, hiscore;
char score_str[7] = "000000", hi_str[7] = "000000";
uint8_t wave, state;
int16_t state_timer;
uint8_t title_blink;
int16_t bul_x[1 + MAX_ALIEN_BULLETS], bul_y[1 + MAX_ALIEN_BULLETS];
uint8_t bul_active[1 + MAX_ALIEN_BULLETS];
int16_t ufo_x;
int8_t ufo_dir;
uint8_t ufo_active;
int16_t ufo_timer, ufo_score_val;
uint8_t shield_px[SHIELD_COUNT][SHIELD_PH][SHIELD_PW];
int16_t shield_x[SHIELD_COUNT];
uint8_t shield_dirty;
int16_t ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
uint8_t ex_timer[MAX_EXPLOSIONS], ex_active[MAX_EXPLOSIONS], ex_color[MAX_EXPLOSIONS];
uint8_t ev_shoot, ev_alien_hit, ev_player_hit, ev_ufo_hit, ev_march;
uint8_t march_note;
uint8_t hit_row;
static int16_t eat_gx, eat_gy = -1;     /* where the swarm was when it last ate shields */

/* port: the SDK's xorshift instead of the 32-bit LCG (a 32-bit multiply is
   ~1000 cycles on the 65SC02); the same 15-bit range */
#define game_rand() (rnd16() & 0x7FFF)

static const uint8_t alien_points[] = { 30, 20, 10 };
const uint8_t col_x[ALIEN_COLS + 1] = { 0, 26, 52, 78, 104, 130, 156, 182, 208 };
const uint8_t row_y[ALIEN_ROWS] = { 0, 14, 28, 42, 56, 70 };
#define alien_type_for_row(row) ((row) <= 1 ? ALIEN_TYPE_C : (row) <= 3 ? ALIEN_TYPE_B : ALIEN_TYPE_A)

/* port: the score is also kept as 6 decimal digits for the status line (no
   32-bit divides to print it) */
static void dec_add(uint8_t i, uint8_t v)     /* v (0..9) at digit i, with carry */
{
    while (v) {
        score_str[i] += v;
        v = 0;
        if (score_str[i] > '9') {
            score_str[i] -= 10;
            v = 1;
            if (i == 0) break;
            --i;
        }
    }
}

static void add_score(uint16_t p)
{
    uint8_t h = 0, t = 0;
    score += p;
    while (p >= 100) { p -= 100; ++h; }
    while (p >= 10) { p -= 10; ++t; }   /* points are multiples of 10 */
    dec_add(4, t);
    dec_add(3, h);
    if (score > hiscore) {
        hiscore = score;
        memcpy(hi_str, score_str, 6);
    }
}

static void init_shields(void)
{
    uint8_t s;
    for (s = 0; s < SHIELD_COUNT; s++) {
        shield_x[s] = (SCREEN_W / (SHIELD_COUNT + 1)) * (s + 1) - SHIELD_W / 2;
        memcpy(shield_px[s], shield_pattern, sizeof shield_px[s]);
    }
    shield_dirty = 7;
}

static void spawn_explosion(int16_t x, int16_t y, uint8_t color)
{
    uint8_t i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!ex_active[i]) {
            ex_x[i] = x;
            ex_y[i] = y;
            ex_timer[i] = 10;
            ex_active[i] = 1;
            ex_color[i] = color;
            return;
        }
    }
}

void game_init(void)
{
    state = STATE_TITLE;
    lives = 3;
    wave = 1;
    player_x = SCREEN_W / 2 - PLAYER_W / 2;
}

void game_init_wave(void)
{
    uint8_t i;

    memset(alive_mask, 0xFF, sizeof alive_mask);
    grid_x = ALIEN_START_X;
    grid_y = ALIEN_START_Y;
    swarm_dir = 1;
    move_timer = 0;
    anim_frame = 0;
    alive_count = ALIEN_ROWS * ALIEN_COLS;
    fire_timer = 0;

    /* Speed increases each wave */
    move_delay = wave >= 3 ? 1 : 2;
    fire_delay = wave >= 6 ? 8 : (uint8_t)(30 - (wave - 1) * 5);    /* 30, 25 .. 10, then 8 */

    for (i = 0; i <= MAX_ALIEN_BULLETS; i++) bul_active[i] = 0;
    ufo_active = 0;
    ufo_timer = 300 + (game_rand() % 400);
    for (i = 0; i < MAX_EXPLOSIONS; i++) ex_active[i] = 0;
    init_shields();
    player_x = SCREEN_W / 2 - PLAYER_W / 2;
    state = STATE_PLAYING;
    state_timer = 0;
    march_note = 0;
    eat_gy = -1;
    ev_shoot = ev_alien_hit = ev_player_hit = ev_ufo_hit = ev_march = 0;
}

/* The original cleared a disc of radius 2 world pixels around the hit
   (21 world pixels); port: at half size, a plus of 5 pixels (20 world). */
static void damage_shield(uint8_t s, int8_t sx, int8_t sy)
{
    uint8_t *row = shield_px[s][0];
    if (sy > 0) row[((sy - 1) << 4) + sx] = 0;
    if (sy < SHIELD_PH - 1) row[((sy + 1) << 4) + sx] = 0;
    row += sy << 4;
    row[sx] = 0;
    if (sx > 0) row[sx - 1] = 0;
    if (sx < SHIELD_PW - 1) row[sx + 1] = 0;
    shield_dirty |= 1 << s;
}

/* bullet i vs the shields; returns 1 if it hit */
static uint8_t check_bullet_shield(uint8_t i)
{
    uint8_t s;
    int16_t bx, by;
    by = bul_y[i] - SHIELD_Y;
    if (by < 0 || by >= SHIELD_H) return 0;
    by >>= 1;
    for (s = 0; s < SHIELD_COUNT; s++) {
        bx = bul_x[i] - shield_x[s];
        if (bx >= 0 && bx < SHIELD_W) {
            bx >>= 1;
            if (shield_px[s][by][bx]) {
                damage_shield(s, (int8_t)bx, (int8_t)by);
                bul_active[i] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int8_t find_lowest_alien(uint8_t col)
{
    int8_t row;
    uint8_t bit = 1 << col;
    for (row = ALIEN_ROWS - 1; row >= 0; row--)
        if (alive_mask[row] & bit) return row;
    return -1;
}

static void alien_fire(void)
{
    int8_t row;
    uint8_t slot, tries, col;
    for (slot = 1; slot <= MAX_ALIEN_BULLETS; slot++)
        if (!bul_active[slot]) break;
    if (slot > MAX_ALIEN_BULLETS) return;
    for (tries = 0; tries < 22; tries++) {
        col = rnd8() & (ALIEN_COLS - 1);
        row = find_lowest_alien(col);
        if (row >= 0) {
            bul_x[slot] = ALIEN_X(row, col) + ALIEN_W / 2;
            bul_y[slot] = ALIEN_Y(row) + ALIEN_H;
            bul_active[slot] = 1;
            return;
        }
    }
}

static uint8_t low_col(uint8_t m)
{
    uint8_t c = 0;
    while (!(m & 1)) { m >>= 1; ++c; }
    return c;
}

static uint8_t high_col(uint8_t m)
{
    uint8_t c = 7;
    while (!(m & 0x80)) { m <<= 1; --c; }
    return c;
}

/* The aliens eat the shields where they overlap them. port (speed, same
   pixels): only when the swarm has moved (the shields can't grow back),
   only the rows crossing the shields' band, rectangles cleared per alien
   (the original tested every shield pixel against every alien). */
static void eat_shields(void)
{
    static uint8_t r, c, s, m, y0, y1, x0, x1, y;
    static int16_t ax, ay, sx, a0, a1;
    for (r = 0; r < ALIEN_ROWS; r++) {
        m = alive_mask[r];
        ay = ALIEN_Y(r);
        if (!m || ay + ALIEN_H <= SHIELD_Y || ay >= SHIELD_Y + SHIELD_H) continue;
        y0 = ay > SHIELD_Y ? (uint8_t)(ay - SHIELD_Y) >> 1 : 0;
        y1 = ay + ALIEN_H < SHIELD_Y + SHIELD_H ? (uint8_t)(ay + ALIEN_H - SHIELD_Y + 1) >> 1 : SHIELD_PH;
        for (c = 0; c < ALIEN_COLS; c++, m >>= 1) {
            if (!(m & 1)) continue;
            ax = ALIEN_X(r, c);
            for (s = 0; s < SHIELD_COUNT; s++) {
                sx = shield_x[s];
                if (ax + ALIEN_W <= sx || ax >= sx + SHIELD_W) continue;
                a0 = ax - sx;
                a1 = a0 + ALIEN_W;
                x0 = a0 > 0 ? (uint8_t)a0 >> 1 : 0;
                x1 = a1 < SHIELD_W ? (uint8_t)(a1 + 1) >> 1 : SHIELD_PW;
                for (y = y0; y < y1; ++y)
                    memset(&shield_px[s][y][x0], 0, x1 - x0);
                shield_dirty |= 1 << s;
            }
        }
    }
}

static void update_playing(uint8_t left, uint8_t right, uint8_t fire_pressed)
{
    static uint8_t i, r, c;
    static int16_t bullet_prev_y, ax, ay;

    ev_shoot = ev_alien_hit = ev_player_hit = ev_ufo_hit = ev_march = 0;

    /* Player movement */
    if (left) player_x -= PLAYER_SPEED;
    if (right) player_x += PLAYER_SPEED;
    if (player_x < PLAYER_MIN_X) player_x = PLAYER_MIN_X;
    if (player_x > PLAYER_MAX_X) player_x = PLAYER_MAX_X;

    /* Player fire */
    if (fire_pressed && !bul_active[0]) {
        bul_x[0] = player_x + PLAYER_W / 2;
        bul_y[0] = PLAYER_Y - 2;
        bul_active[0] = 1;
        ev_shoot = 1;
    }

    /* Player bullet; the previous y for the swept test */
    bullet_prev_y = bul_y[0];
    if (bul_active[0]) {
        bul_y[0] -= PLAYER_BULLET_SPEED;
        if (bul_y[0] < BULLET_TOP) bul_active[0] = 0;
        check_bullet_shield(0);
    }

    /* Player bullet vs aliens, swept over [y, previous y]. port (speed, same
       result): only the rows the sweep crosses, top first as the original's
       loop, and the one column under the bullet */
    if (bul_active[0]) {
        int16_t bx = bul_x[0], by_lo = bul_y[0], by_hi = bullet_prev_y, rel;
        for (r = 0; r < ALIEN_ROWS; r++) {
            ay = ALIEN_Y(r);
            if (!(by_hi >= ay && by_lo < ay + ALIEN_H)) continue;
            rel = bx - grid_x - ALIEN_ROW_SHIFT(r);
            if (rel < 0 || rel >= ALIEN_COLS * ALIEN_CELL_W) continue;
            for (c = 0; rel >= col_x[c + 1]; ++c)
                ;
            if (rel - col_x[c] >= ALIEN_W) continue;
            if (!(alive_mask[r] & (1 << c))) continue;
            ax = ALIEN_X(r, c);
            alive_mask[r] &= ~(1 << c);
            alive_count--;
            bul_active[0] = 0;
            add_score(alien_points[alien_type_for_row(r)]);
            hit_row = r;
            ev_alien_hit = 1;
            spawn_explosion(ax + ALIEN_W / 2, ay + ALIEN_H / 2, COL_WHITE);
            break;
        }
    }

    /* Player bullet vs the comet */
    if (bul_active[0] && ufo_active) {
        int16_t bx = bul_x[0], by = bul_y[0];
        if (bx >= ufo_x && bx < ufo_x + UFO_W && by >= UFO_Y && by < UFO_Y + UFO_H) {
            static const uint16_t ufo_scores[] = { 50, 100, 150, 300 };
            ufo_active = 0;
            bul_active[0] = 0;
            ufo_score_val = ufo_scores[game_rand() & 3];
            add_score(ufo_score_val);
            ev_ufo_hit = 1;
            spawn_explosion(ufo_x + UFO_W / 2, UFO_Y + UFO_H / 2, COL_RED);
        }
    }

    /* Alien movement: fewer aliens = faster */
    move_timer++;
    {
        uint8_t alive = alive_count, delay, step;
        if (alive <= 20) delay = 1;
        else if (alive <= 35) delay = 2;
        else delay = move_delay;
        if (alive <= 1) step = 10;
        else if (alive <= 5) step = 8;
        else if (alive <= 10) step = 5;
        else if (alive <= 20) step = 4;
        else step = 3;

        if (move_timer >= delay) {
            int16_t left_edge = SCREEN_W, right_edge = 0;
            uint8_t drop = 0;
            move_timer = 0;
            anim_frame ^= 1;
            ev_march = 1;
            march_note = march_note == 2 ? 0 : march_note + 1;

            /* the swarm's bounds (port: from each row's first and last alien) */
            for (r = 0; r < ALIEN_ROWS; r++) {
                if (!alive_mask[r]) continue;
                ax = ALIEN_X(r, low_col(alive_mask[r]));
                if (ax < left_edge) left_edge = ax;
                ax = ALIEN_X(r, high_col(alive_mask[r])) + ALIEN_CELL_W;
                if (ax > right_edge) right_edge = ax;
            }
            if (swarm_dir > 0 && right_edge >= SCREEN_W - 8) drop = 1;
            if (swarm_dir < 0 && left_edge <= 8) drop = 1;
            if (drop) {
                grid_y += ALIEN_DROP;
                swarm_dir = -swarm_dir;
            } else {
                grid_x += swarm_dir > 0 ? step : -step;
            }
        }
    }

    /* Alien firing */
    if (++fire_timer >= fire_delay) {
        fire_timer = 0;
        alien_fire();
    }

    /* Alien bullets */
    for (i = 1; i <= MAX_ALIEN_BULLETS; i++) {
        if (!bul_active[i]) continue;
        bul_y[i] += ALIEN_BULLET_SPEED;
        if (bul_y[i] > GROUND_Y) {
            bul_active[i] = 0;
            continue;
        }
        if (check_bullet_shield(i)) continue;
        if (bul_x[i] >= player_x && bul_x[i] < player_x + PLAYER_W &&
            bul_y[i] >= PLAYER_Y && bul_y[i] < PLAYER_Y + PLAYER_H) {
            bul_active[i] = 0;
            ev_player_hit = 1;
            state = STATE_DYING;
            state_timer = 60;
            spawn_explosion(player_x + PLAYER_W / 2, PLAYER_Y + PLAYER_H / 2, COL_YELLOW);
        }
    }

    /* The comet */
    if (!ufo_active) {
        if (--ufo_timer <= 0) {
            ufo_active = 1;
            ufo_dir = (game_rand() & 1) ? 1 : -1;
            ufo_x = ufo_dir > 0 ? -UFO_W : SCREEN_W;
        }
    } else {
        ufo_x += ufo_dir;
        if (ufo_x < -UFO_W - 10 || ufo_x > SCREEN_W + 10) {
            ufo_active = 0;
            ufo_timer = 400 + (game_rand() % 600);
        }
    }

    /* Explosions */
    for (i = 0; i < MAX_EXPLOSIONS; i++)
        if (ex_active[i] && --ex_timer[i] == 0) ex_active[i] = 0;

    /* Aliens destroy shields on contact; game over when they reach the gun */
    {
        int16_t bottom_alien_y = 0;
        for (r = ALIEN_ROWS; r-- > 0;)
            if (alive_mask[r]) { bottom_alien_y = ALIEN_Y(r) + ALIEN_H; break; }
        if (bottom_alien_y >= SHIELD_Y && (grid_x != eat_gx || grid_y != eat_gy)) {
            eat_shields();
            eat_gx = grid_x;
            eat_gy = grid_y;
        }
        if (bottom_alien_y >= PLAYER_Y) {
            state = STATE_GAMEOVER;
            state_timer = 120;
        }
    }

    /* Wave clear */
    if (alive_count == 0) {
        state = STATE_WAVE_CLEAR;
        state_timer = 90;
    }
}

void game_update(uint8_t left, uint8_t right, uint8_t fire_pressed)
{
    /* port: the events last one frame in every state. The original cleared
       them only in play, so while the gun was dying (or a wave was cleared)
       its last sound restarted every frame. */
    ev_shoot = ev_alien_hit = ev_player_hit = ev_ufo_hit = ev_march = 0;
    switch (state) {
    case STATE_TITLE:
        title_blink++;
        if (fire_pressed) {
            score = 0;
            memcpy(score_str, "000000", 6);
            lives = 3;
            wave = 1;
            game_init_wave();
        }
        break;
    case STATE_PLAYING:
        update_playing(left, right, fire_pressed);
        break;
    case STATE_DYING:
        if (--state_timer <= 0) {
            if (--lives <= 0) {
                state = STATE_GAMEOVER;
                state_timer = 150;
            } else {
                state = STATE_PLAYING;
                player_x = SCREEN_W / 2 - PLAYER_W / 2;
            }
        }
        break;
    case STATE_GAMEOVER:
        if (--state_timer <= 0 || fire_pressed) state = STATE_TITLE;
        break;
    case STATE_WAVE_CLEAR:
        if (--state_timer <= 0) {
            wave++;
            game_init_wave();
        }
        break;
    }
}
