/* Unpacks a grid of a gridpack.py set (see gridpack.h; gridpack.unpack in
   sdk/tools is the reference): a 16-bit binary arithmetic decoder. */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "gridpack.h"

/* port (speed): decode() and its bit reader are in gpdecode.s (the C is
   the ZX Spectrum SDK's gridpack.c, kept below) */
extern const uint8_t *gp_bp;
extern uint8_t gp_left;
extern uint16_t gp_rng, gp_code;
extern uint8_t __fastcall__ gp_decode(uint8_t p);
extern uint8_t gp_next_bit(void);
#define bp gp_bp
#define left gp_left
#define rng gp_rng
#define code gp_code
#define decode gp_decode
#define next_bit gp_next_bit
#if 0
static const uint8_t *bp;
static uint8_t cur, left;
static uint16_t rng, code;

static uint8_t next_bit(void)
{
    uint8_t b;
    if (left == 0) {
        cur = *bp++;
        left = 8;
    }
    left--;
    b = cur >> 7;
    cur <<= 1;
    return b;
}

/* one decision; p = P(0) in 1/256ths */
static uint8_t decode(uint8_t p)
{
    uint16_t bound = (uint16_t)(uint8_t)(rng >> 8) * p;
    uint8_t b = 0;
    if (code < bound) {
        rng = bound;
    } else {
        code -= bound;
        rng -= bound;
        b = 1;
    }
    while (rng < 0x8000) {
        rng <<= 1;
        code = (code << 1) | next_bit();
    }
    return b;
}

#endif

/* port (speed, the same steps as the ZX Spectrum SDK's gp_unpack): byte
   indexes into fixed copies of the set's tables (no multiplies), the tile
   above from the previous row's copy, and "is there another candidate after
   k" from a count instead of a search */
#pragma zpsym ("gp_bp")
void gp_unpack(const gp_set *set, uint16_t index, uint8_t *out)
{
    static uint8_t n, w, prev, i, x, above, t, ruled, k, s, cands, *o;
    static uint8_t row_above[32];
    static const uint8_t *pa, *pb, *pl, *order, *len;
    static uint16_t cells;

    n = set->ntiles;
    w = set->w;
    pa = set->pa;
    pb = set->pb;
    order = set->order;
    len = set->grid_len;
    bp = set->bits;
    while (index--) bp += *len++;
    left = 0;
    rng = 0xFFFF;
    code = 0;
    for (i = 0; i < 16; i++) code = (code << 1) | next_bit();

    memset(row_above, n, sizeof(row_above));
    prev = n;
    o = out;
    for (cells = (uint16_t)w * set->h, x = 0; cells; --cells) {
        above = row_above[x];
        k = prev * (n + 1) + above;     /* (n + 1)^2 < 256 */
        if (decode(pa[k])) {
            t = prev;
        } else if (above != prev && above != n && decode(pb[k])) {
            t = above;
        } else {
            /* "is it order[k]?" skipping the two ruled out; the last needs no question */
            ruled = above != n ? above : prev;
            pl = set->pl + above * n;
            cands = n - 1 - (ruled != prev && ruled < n);   /* tiles not ruled out */
            if (prev >= n) cands = n - (ruled < n);
            t = 255;
            for (k = 0; k < n; k++) {
                s = order[k];
                if (s == prev || s == ruled) continue;
                if (--cands == 0 || decode(pl[k])) { t = s; break; }
            }
        }
        *o++ = t;
        row_above[x] = t;
        prev = t;
        if (++x == w) x = 0;
    }
}
