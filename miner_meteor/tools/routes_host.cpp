// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
/*
 * Miner Meteor routes on the host, over this port's own logic (src/game.c,
 * plat.c, tmap.c, levels.c, levels_data.c; tm_scan, which is 6502 in
 * tmap_scan.s, is C here). Built and run by tools/routes.sh.
 *
 *   routes_host check tools/routes.json   play each route from its cavern's
 *                                         load; exit 0 if all six clear, each
 *                                         without a death and never standing
 *                                         on air (the old snap's bob)
 *   routes_host search N [BEAM]           breadth-first search, one layer a
 *                                         tick, inputs none/L/R/J/L+J/R+J;
 *                                         prints the shortest route that
 *                                         clears cavern N (1-6) without a death
 *
 * The search keeps one state per (player, keys, tile kinds, falling floors
 * and their timers, guardians) over all ticks, so with no BEAM it is
 * exhaustive: "no route" means there is none. With BEAM, a layer keeps the
 * BEAM states with the fewest keys left, then nearest a key or the open
 * exit (faster for Collapsing Attic, whose falling-floor timers make the
 * exhaustive search very large); a route it finds is real either way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>
extern "C" {
#include "game.h"
#include "tmap.h"
#include "levels_data.h"
uint16_t tm_first;
extern uint16_t tm_n;
uint16_t tm_scan(uint8_t id)
{
    uint16_t n = 0, i;
    tm_first = 0xFFFF;
    for (i = 0; i < tm_n; i++)
        if (tmap_cells[i] == id) { if (tm_first == 0xFFFF) tm_first = i; n++; }
    return n;
}
}

typedef std::vector<std::pair<int, int> > Route;

static std::vector<Route> load(const char *path)
{
    std::vector<Route> routes;
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    int depth = 0, c, have = 0, num = 0, first = -1;
    while ((c = fgetc(f)) != EOF) {
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); have = 1; continue; }
        if (have) {
            if (first < 0) first = num;
            else { routes.back().push_back(std::make_pair(first, num)); first = -1; }
            num = 0; have = 0;
        }
        if (c == '[' && ++depth == 2) routes.push_back(Route());
        if (c == ']') depth--;
    }
    fclose(f);
    return routes;
}

static void start(int lv)
{
    game_init();
    gs.mode = GM_PLAYING;
    game_load_level((uint8_t)lv);
}

static int check(const char *path)
{
    std::vector<Route> routes = load(path);
    int cleared = 0;
    for (int lv = 0; lv < num_levels; lv++) {
        start(lv);
        uint8_t lives = gs.lives;
        int ticks = 0, done = 0, unaligned = 0;
        const char *why = "route ended";
        if (lv < (int)routes.size())
            for (size_t r = 0; r < routes[lv].size() && !done; r++)
                for (int n = 0; n < routes[lv][r].second && !done; n++) {
                    game_tick((uint8_t)routes[lv][r].first);
                    ticks++;
                    /* on the ground with no floor under the feet = the old
                       snap's bob (a floor that crumbled this tick aside) */
                    if (gs.mode == GM_PLAYING && gs.player.b.on_ground && !gs.num_changed &&
                        !plat_solid_at(gs.player.b.x + 1, gs.player.b.y + 16) &&
                        !plat_solid_at(gs.player.b.x + 14, gs.player.b.y + 16)) unaligned++;
                    if (unaligned) { why = "stood off the floor (bob)"; done = -1; }
                    else if (gs.mode == GM_LEVELWIN || gs.mode == GM_WIN) done = 1;
                    else if (gs.lives < lives || gs.mode == GM_LOSE) { why = "lost a life"; done = -1; }
                }
        if (done == 1) cleared++;
        printf("cavern %d %-16s %s after %d ticks (keys left %d, ticks standing on air %d)\n", lv + 1,
               lv_names[lv], done == 1 ? "cleared" : why, ticks, gs.keys_remaining, unaligned);
    }
    printf("%d of %d caverns cleared\n", cleared, (int)num_levels);
    return cleared == num_levels ? 0 : 1;
}

struct H { unsigned long long a, b; bool operator==(const H &o) const { return a == o.a && b == o.b; } };
struct HH { size_t operator()(const H &h) const { return (size_t)(h.a ^ (h.b * 0x9e3779b97f4a7c15ULL)); } };
struct Step { int parent; uint8_t in; };

static H key_of(void)
{
    std::string k;
    const plat_body &b = gs.player.b;
    int16_t p[5] = { b.x, b.y, (int16_t)b.vy, (int16_t)b.on_ground, (int16_t)gs.player.jump_lock };
    k.append((const char *)p, sizeof(p));
    k.push_back((char)gs.keys_remaining);
    k.append((const char *)gs.kind, sizeof(gs.kind));
    for (int i = 0; i < gs.num_falling; i++) {
        k.append((const char *)&gs.falling[i].cell, 2);
        k.push_back((char)gs.falling[i].timer);
    }
    for (int i = 0; i < gs.num_guards; i++) {
        int16_t g[4] = { gs.guards[i].x, gs.guards[i].y, gs.guards[i].dx, gs.guards[i].dy };
        k.append((const char *)g, sizeof(g));
    }
    unsigned long long a = 1469598103934665603ULL, c = 0x84222325cbf29ce4ULL;
    for (unsigned char ch : k) { a = (a ^ ch) * 1099511628211ULL; c = (c ^ ch) * 0x100000001b3ULL; c ^= c >> 29; }
    H h = { a, c };
    return h;
}

static long dist_to_goal(void)
{
    long best = 1L << 30;
    for (int r = 0; r < GRID_H; r++)
        for (int c = 0; c < GRID_W; c++) {
            uint8_t k = gs.kind[r * GRID_W + c];
            if (k == T_KEY || (k == T_EXIT && gs.keys_remaining == 0)) {
                long dx = c * TILE_W - gs.player.b.x, dy = r * TILE_H - gs.player.b.y;
                long d = (dx < 0 ? -dx : dx) + 2 * (dy < 0 ? -dy : dy);
                if (d < best) best = d;
            }
        }
    return best;
}

static int search(int lv, size_t beam)
{
    static const uint8_t inputs[] = { 0, INPUT_LEFT, INPUT_RIGHT, INPUT_JUMP,
                                      INPUT_LEFT | INPUT_JUMP, INPUT_RIGHT | INPUT_JUMP };
    start(lv);
    std::vector<GameState> cur(1, gs), next;
    std::vector<std::vector<Step> > hist;
    std::unordered_set<H, HH> seen;
    for (int depth = 1; depth <= 6000 && !cur.empty(); depth++) {
        std::vector<Step> steps;
        std::vector<long> score;
        next.clear();
        for (size_t ci = 0; ci < cur.size(); ci++)
            for (uint8_t in : inputs) {
                gs = cur[ci];
                game_tick(in);
                if (gs.lives < cur[ci].lives || gs.mode == GM_LOSE) continue;
                if (gs.mode == GM_LEVELWIN || gs.mode == GM_WIN) {
                    std::vector<uint8_t> path(1, in);
                    for (int d = (int)hist.size() - 1, p = (int)ci; d >= 0; d--) {
                        path.push_back(hist[d][p].in);
                        p = hist[d][p].parent;
                    }
                    std::reverse(path.begin(), path.end());
                    fprintf(stderr, "cavern %d cleared in %d ticks\n", lv + 1, depth);
                    printf("[");
                    for (size_t i = 0; i < path.size();) {
                        size_t j = i;
                        while (j < path.size() && path[j] == path[i]) j++;
                        printf("%s[%d, %d]", i ? ", " : "", path[i], (int)(j - i));
                        i = j;
                    }
                    printf("]\n");
                    return 0;
                }
                if (!seen.insert(key_of()).second) continue;
                Step s = { (int)ci, in };
                steps.push_back(s);
                next.push_back(gs);
                score.push_back(beam ? gs.keys_remaining * 100000L + dist_to_goal() : 0);
            }
        if (beam && next.size() > beam) {
            std::vector<size_t> idx(next.size());
            for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
            std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return score[a] < score[b]; });
            idx.resize(beam);
            std::sort(idx.begin(), idx.end());
            std::vector<GameState> kept;
            std::vector<Step> ks;
            for (size_t i : idx) { kept.push_back(next[i]); ks.push_back(steps[i]); }
            next.swap(kept);
            steps.swap(ks);
        }
        hist.push_back(steps);
        cur.swap(next);
    }
    fprintf(stderr, "cavern %d: no route%s\n", lv + 1, beam ? " (beam)" : "");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "check")) return check(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "search"))
        return search(atoi(argv[2]) - 1, argc > 3 ? (size_t)atol(argv[3]) : 0);
    fprintf(stderr, "usage: routes_host check ROUTES.json | search N [BEAM]\n");
    return 2;
}
