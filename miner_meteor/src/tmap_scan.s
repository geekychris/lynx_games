; tmap_scan.s - tmap_count / tmap_find's search in 6502 (tmap.c): the C
; loops over the 300 cells took 35k and 18k cycles when a cavern loads.
;
; uint16_t __fastcall__ tm_scan(uint8_t id)
;   counts the cells of the map (tm_n bytes from tmap_cells) equal to id;
;   tm_first = the index of the first one, $FFFF if none.

        .export _tm_scan, _tm_first
        .import _tmap_cells, _tm_n
        .importzp ptr1, tmp1, tmp2, tmp3, tmp4

        .bss
_tm_first:      .res 2

        .code
_tm_scan:
        sta tmp1                ; id
        lda _tmap_cells
        sta ptr1
        lda _tmap_cells+1
        sta ptr1+1
        lda #$FF
        sta _tm_first
        sta _tm_first+1
        lda #0
        sta tmp2                ; count
        sta tmp3
        sta tmp4                ; page of the index
        ldx _tm_n+1             ; whole pages
        beq last
page:   ldy #0
pl:     lda (ptr1),y
        cmp tmp1
        bne :+
        jsr hit
:       iny
        bne pl
        inc ptr1+1
        inc tmp4
        dex
        bne page
last:   ldy #0
ll:     cpy _tm_n               ; the rest of the last page
        beq done
        lda (ptr1),y
        cmp tmp1
        bne :+
        jsr hit
:       iny
        bne ll
done:   lda tmp2
        ldx tmp3
        rts

hit:    inc tmp2
        bne :+
        inc tmp3
:       lda _tm_first+1
        cmp #$FF
        bne :+
        sty _tm_first
        lda tmp4
        sta _tm_first+1
:       rts
