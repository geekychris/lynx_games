; uint8_t tile_px(int16_t px, int16_t py): the tile under a world pixel,
; level_get_tile(px / 16, py / 16) with C's rounding towards zero (so
; -15..-1 is tile 0), TILE_EMPTY off the map. In C it was 1.6k cycles; the
; enemies make about six lookups each a tick.
; tile_xy: the same for asm: px in tp_x, py in A/X; returns the tile in A.
        .export _tile_px, tile_xy, tp_x
        .import _tmap_row, _tmap_w, _tmap_h, incsp2
        .importzp c_sp, tmp1

        .zeropage
tp_x:   .res 2
tp_row: .res 2

        .code
_tile_px:
        pha
        ldy #1
        lda (c_sp),y
        sta tp_x+1
        dey
        lda (c_sp),y
        sta tp_x
        pla
        jsr tile_xy
        jmp incsp2

tile_xy:
        cpx #0                  ; py
        beq @ypos
        cpx #$FF
        bne @out
        cmp #$F1                ; -15..-1: row 0
        bcc @out
        lda #0
        beq @yok
@ypos:  lsr a
        lsr a
        lsr a
        lsr a
@yok:   cmp _tmap_h
        bcs @out
        asl a
        tay
        lda _tmap_row,y
        sta tp_row
        lda _tmap_row+1,y
        sta tp_row+1
        lda tp_x+1              ; px
        bmi @xneg
        cmp #16
        bcs @out
        sta tmp1
        lda tp_x
        lsr tmp1
        ror a
        lsr tmp1
        ror a
        lsr tmp1
        ror a
        lsr tmp1
        ror a
        jmp @xok
@xneg:  cmp #$FF
        bne @out
        lda tp_x
        cmp #$F1
        bcc @out
        lda #0
@xok:   cmp _tmap_w
        bcs @out
        tay
        lda (tp_row),y
        ldx #0
        rts
@out:   lda #0
        ldx #0
        rts
