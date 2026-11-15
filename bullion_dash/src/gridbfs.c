/* Grid pathfinding through a distance field (see gridbfs.h). */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "gridbfs.h"

/* shared with gridbfs_run.s (port: the queue is a byte index into a
   256-byte page of 2-byte cell offsets, where the Spectrum's asm kept
   addresses) */
uint8_t *gb_cells;              /* (flags, dist) pairs, row stride gb_stride2 bytes */
uint16_t gb_stride2;            /* 2 * (w + 1) */
uint8_t gb_qhead, gb_qtail;     /* the search queue: indexes into gb_queue */
uint8_t *gb_want;               /* gb_run stops once this cell's distance is known */
uint16_t gb_searched;
uint8_t gb_overflow;

uint16_t *gb_queue;
static uint8_t gb_w, gb_h, gb_valid;
static uint8_t gb_graph_same;   /* no flags changed since the field was made */
static uint8_t gb_tx = 255, gb_ty = 255;

#define CELL(x, y) (gb_cells + (uint16_t)((y) + 1) * gb_stride2 + 2 * (uint16_t)(x))

#define FAR 253                 /* known to have no path (carried over, see field_start) */

/* gridbfs_run.s: the search loop and the field's reset (the C they replace
   is the ZX Spectrum SDK's gridbfs.c host version) */
extern void gb_run(void);
extern void __fastcall__ gb_clear(uint8_t carry);

void gb_init(uint8_t *cells, uint16_t *queue, uint8_t w, uint8_t h)
{
    gb_cells = cells;
    gb_queue = queue;
    gb_w = w;
    gb_h = h;
    gb_stride2 = 2 * ((uint16_t)w + 1);
    memset(cells, 0, GB_CELLS(w, h));
    gb_valid = gb_graph_same = 0;
    gb_tx = gb_ty = 255;
    gb_searched = 0;
    gb_overflow = 0;
}

void gb_set(uint8_t x, uint8_t y, uint8_t flags)
{
    uint8_t *c = CELL(x, y);
    if (*c != flags) {
        *c = flags;
        gb_valid = 0;
        gb_graph_same = 0;
    }
}

/* port: the caller changed many cells' flags itself */
void gb_changed(void)
{
    gb_valid = 0;
    gb_graph_same = 0;
}

uint8_t gb_get(uint8_t x, uint8_t y)
{
    return *CELL(x, y);
}

void gb_target(uint8_t x, uint8_t y)
{
    if (x != gb_tx || y != gb_ty) {
        gb_tx = x;
        gb_ty = y;
        gb_valid = 0;
    }
}

/* A new field. When only the target moved, the old field was searched to
   the end, and the new target had a path in it: a cell with no path to the
   old target has none to the new one either (it would reach the old target
   through it), so it keeps its "no path" and a query doesn't search the
   whole grid again to find out (a guard that can't reach the player). */
static void field_start(void)
{
    uint8_t *t = CELL(gb_tx, gb_ty);
    uint8_t carry = gb_graph_same && gb_qhead == gb_qtail && !gb_overflow && t[1] < FAR;
    gb_clear(carry);
    gb_graph_same = 1;
    gb_qhead = gb_qtail = 0;
    gb_overflow = 0;
    t[1] = 0;
    if (t[0] & GB_IN) {
        gb_queue[0] = (uint16_t)(t - gb_cells);
        gb_qtail = 2;
    }
    gb_valid = 1;
}

static uint8_t *known(uint8_t x, uint8_t y)
{
    uint8_t *s = CELL(x, y);
    if (!gb_valid) field_start();
    if (s[1] == 255 && gb_qhead != gb_qtail) {
        gb_want = s;
        gb_run();
    }
    return s;
}

uint8_t gb_dist(uint8_t x, uint8_t y)
{
    uint8_t d = known(x, y)[1];
    return gb_overflow || d >= FAR ? 255 : d;
}

uint8_t gb_step(uint8_t x, uint8_t y)
{
    uint8_t *s = known(x, y), d = s[1] - 1, f = s[0];
    if (s[1] == 0) return 0;
    if (s[1] >= FAR || gb_overflow) return GB_NONE;
    /* the neighbours one step closer are already labelled (BFS order) */
    if ((f & GB_L) && (s[-2] & GB_IN) && s[-1] == d) return GB_L;
    if ((f & GB_R) && (s[2] & GB_IN) && s[3] == d) return GB_R;
    if ((f & GB_U) && (*(s - gb_stride2) & GB_IN) && *(s - gb_stride2 + 1) == d) return GB_U;
    if ((f & GB_D) && (*(s + gb_stride2) & GB_IN) && *(s + gb_stride2 + 1) == d) return GB_D;
    return GB_NONE;
}
