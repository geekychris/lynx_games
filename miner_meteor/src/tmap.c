/* One tile map with a row table (see tmap.h): the ZX Spectrum SDK's tmap.c,
   with plain-byte maps instead of zxrle. */
#pragma static-locals(on)
#include <stdint.h>
#include "tmap.h"
#include <string.h>

uint8_t *tmap_cells;
uint8_t tmap_w, tmap_h;
uint8_t *tmap_row[TMAP_MAX_H];
uint16_t tm_n;                      /* port: w * h, for tm_scan (tmap_scan.s) */
extern uint16_t __fastcall__ tm_scan(uint8_t id);
extern uint16_t tm_first;

void tmap_init(uint8_t *cells, uint8_t w, uint8_t h)
{
    uint8_t r;
    tmap_cells = cells;
    tmap_w = w;
    tmap_h = h;
    tm_n = (uint16_t)w * h;
    for (r = 0; r < h; r++, cells += w) tmap_row[r] = cells;
}

/* port: the caverns are stored as plain bytes (levels_data.c), not zxrle */
uint16_t tmap_load(const uint8_t *cells)
{
    uint16_t n = (uint16_t)tmap_w * tmap_h;
    memcpy(tmap_cells, cells, n);
    return n;
}

uint8_t tmap_at(int16_t c, int16_t r, uint8_t outside)
{
    if (c < 0 || r < 0 || c >= tmap_w || r >= tmap_h) return outside;
    return tmap_row[r][c];
}

/* port: the search is tm_scan (tmap_scan.s); same results */
uint8_t tmap_find(uint8_t id, uint8_t *col, uint8_t *row)
{
    uint16_t i;
    uint8_t r = 0;
    tm_scan(id);
    i = tm_first;
    if (i == 0xFFFF) return 0;
    while (i >= tmap_w) { i -= tmap_w; ++r; }
    *col = (uint8_t)i;
    *row = r;
    return 1;
}

uint16_t tmap_count(uint8_t id)
{
    return tm_scan(id);
}

uint16_t tmap_replace(uint8_t from, uint8_t to)
{
    uint16_t n = 0, i = (uint16_t)tmap_w * tmap_h;
    uint8_t *p = tmap_cells;
    for (; i--; p++)
        if (*p == from) { *p = to; n++; }
    return n;
}
