; Orb Hunter: the tile properties at a pixel of the room, fast (the
; collision tests call it a dozen times a frame; in C it was ~400 cycles).
;
;   props_px(px, py) = tile_props[tile at (px / 16, py / 16)], where the
;   division truncates towards zero (the original's WORD division: pixels
;   -15..-1 are tile 0) and a tile outside the room is solid rock
;
; In: _ppx, _ppy (16-bit signed). Out: A.

        .export _props_asm, _ppx, _ppy
        .import _room, _tile_props

TPROP_SOLID = 1

        .bss
_ppx:   .res 2
_ppy:   .res 2

        .segment "ZEROPAGE"
rp:     .res 2

        .rodata
row_lo: .repeat 16, r
        .byte <(_room + r * 20)
        .endrepeat
row_hi: .repeat 16, r
        .byte >(_room + r * 20)
        .endrepeat

        .code
_props_asm:
        lda _ppy+1              ; the row: 0..255 -> py / 16
        bne @ny
        lda _ppy
        lsr
        lsr
        lsr
        lsr
        tax
        jmp @col
@ny:    cmp #$FF                ; -15..-1: row 0
        bne @solid
        lda _ppy
        cmp #$F1
        bcc @solid
        ldx #0
@col:   lda row_lo,x
        sta rp
        lda row_hi,x
        sta rp+1
        lda _ppx+1              ; the column: 0..319 -> px / 16
        beq @lo
        cmp #1
        beq @hi
        cmp #$FF                ; -15..-1: column 0
        bne @solid
        lda _ppx
        cmp #$F1
        bcc @solid
        ldy #0
        beq @get
@hi:    lda _ppx                ; 256..319
        cmp #64
        bcs @solid
        lsr
        lsr
        lsr
        lsr
        ora #16
        tay
        bne @get
@lo:    lda _ppx
        lsr
        lsr
        lsr
        lsr
        tay
@get:   lda (rp),y
        tax
        lda _tile_props,x
        rts
@solid: lda #TPROP_SOLID
        rts
