/*
 * Orb Hunter - the world (port: the original keeps all 48 rooms as 15,360
 * bytes, made by levels.c at start-up; here they are its own rooms run-length
 * coded, 1,541 bytes (world_data.c, tools/gen_world.py), the current one
 * decoded into room[][] when it is entered, and what the game changes in
 * them (the doors a rocket opened) in a list applied when a room is decoded.
 * As the Atari 8-bit port.)
 */
#pragma static-locals (on)
#include <string.h>
#include "game.h"
#include "render.h"
#include "lynxlog.h"

extern const uint8_t world_rle[];
extern const uint16_t world_off[48];

UBYTE room[ROOM_H][ROOM_W];
static uint8_t cur_room;

/* the changes: room, tile (ty * 20 + tx), new tile */
#define MAX_CHANGES 48
static uint8_t ch_room[MAX_CHANGES], ch_tile[MAX_CHANGES], ch_val[MAX_CHANGES];
static uint8_t n_changes;

ItemSpawn g_items[MAX_ITEMS];
uint8_t g_num_items;

void items_init(void)
{
    memcpy(g_items, items_start, items_count * sizeof(ItemSpawn));
    g_num_items = items_count;
}

void world_reset(void)
{
    n_changes = 0;
}

void world_load_room(int8_t rx, int8_t ry)
{
    const uint8_t *s;
    uint8_t *d = &room[0][0];
    uint16_t left = ROOM_W * ROOM_H;
    uint8_t i, b, n, t;
    cur_room = (uint8_t)(ry * WORLD_W + rx);
    s = world_rle + world_off[cur_room];
    while (left) {
        b = *s++;
        n = b >> 4;
        t = b & 15;
        left -= n;
        while (n--) *d++ = t;
    }
    for (i = 0; i < n_changes; ++i)
        if (ch_room[i] == cur_room) (&room[0][0])[ch_tile[i]] = ch_val[i];
}

void world_set_tile(uint8_t tx, uint8_t ty, UBYTE t)
{
    uint8_t k = (uint8_t)(ty * ROOM_W + tx), i;
    room[ty][tx] = t;
    lynx_log_s("oh: door open\n");
    render_tile_changed(tx, ty);            /* (repainted in both buffers) */
    for (i = 0; i < n_changes; ++i)
        if (ch_room[i] == cur_room && ch_tile[i] == k) { ch_val[i] = t; return; }
    if (n_changes < MAX_CHANGES) {
        ch_room[n_changes] = cur_room;
        ch_tile[n_changes] = k;
        ch_val[n_changes] = t;
        ++n_changes;
    }
}
