; Orb Hunter - the renderer's hot loops (render.c has the C they replace
; in its comments): tiles into the sprite pool, and the cells an object
; covers into this buffer's repaint list. In C these cost ~1,000 cycles a
; cell and ~800 an object (16-bit maths, 2D indexes); here a few hundred.

        .export _paint_list, _paint_all, _pool_flush, _put_cells
        .export _pool_n, _pl_x, _pl_y, _pl_n
        .import _spr_draw, _room, _tiles_frames, _tile_frame, _lava_phase, _save_phase
        .import _pool, _mark, _stamp, _cell_y, _rd_cells
        .import _ox, _oy, _ow, _oh, _dlx, _dly, _dln, _dfull, _row6

TILE_LAVA   = 5
TILE_SAVE   = 9
POOL        = 20                ; render.c POOL
SCB         = 23                ; sizeof(SCB_REHV_PAL)
DL_MAX      = 120               ; render.c DL_MAX
PF_Y        = 6

        .segment "ZEROPAGE"
rp:     .res 2                  ; a room / mark row
sp_:    .res 2                  ; an SCB
lx:     .res 2                  ; the list being painted / filled
ly:     .res 2

        .bss
_pool_n: .res 1
_pl_x:  .res 2                  ; paint_list's lists and count (set by C)
_pl_y:  .res 2
_pl_n:  .res 1
cnt:    .res 1
idx:    .res 1
cx:     .res 1
cy:     .res 1
cx0:    .res 1
cx1:    .res 1
cy1:    .res 1
dat:    .res 2
x1:     .res 2

        .rodata
room_lo: .repeat 16, r
        .byte <(_room + r * 20)
        .endrepeat
room_hi: .repeat 16, r
        .byte >(_room + r * 20)
        .endrepeat
mark_lo: .repeat 16, r
        .byte <(_mark + r * 20)
        .endrepeat
mark_hi: .repeat 16, r
        .byte >(_mark + r * 20)
        .endrepeat
pool_lo: .repeat POOL + 1, i
        .byte <(_pool + i * SCB)
        .endrepeat
pool_hi: .repeat POOL + 1, i
        .byte >(_pool + i * SCB)
        .endrepeat

        .code

; the tile of cell (cx, cy) into the next pool SCB (pool_cell in C)
cell:   ldx cy
        lda room_lo,x
        sta rp
        lda room_hi,x
        sta rp+1
        ldy cx
        lda (rp),y              ; the tile
        tax
        lda _tile_frame,x
        cpx #TILE_LAVA
        bne @nl
        clc
        adc _lava_phase
        bra @f
@nl:    cpx #TILE_SAVE
        bne @f
        clc
        adc _save_phase
@f:     asl a
        tay
        lda _tiles_frames,y
        sta dat
        lda _tiles_frames+1,y
        sta dat+1
        ldx _pool_n
        lda pool_lo,x
        sta sp_
        lda pool_hi,x
        sta sp_+1
        ldy #5                  ; data
        lda dat
        sta (sp_),y
        iny
        lda dat+1
        sta (sp_),y
        iny                     ; hpos = cx * 8
        lda cx
        asl a
        asl a
        asl a
        sta (sp_),y
        iny
        lda #0
        sta (sp_),y
        iny                     ; vpos
        ldx cy
        lda _cell_y,x
        sta (sp_),y
        iny
        lda #0
        sta (sp_),y
        inc _pool_n
        lda _pool_n
        cmp #POOL
        beq _pool_flush
        rts

; draw the pool's SCBs (one chain) and empty it
_pool_flush:
        ldx _pool_n
        beq @r
        dex
        lda pool_lo,x           ; the last one ends the chain
        sta sp_
        lda pool_hi,x
        sta sp_+1
        ldy #4
        lda #0
        sta (sp_),y
        dey
        sta (sp_),y
        lda #<_pool
        ldx #>_pool
        jsr _spr_draw
        ldx _pool_n             ; link it on again
        lda pool_lo,x
        pha
        lda pool_hi,x
        pha
        dex
        lda pool_lo,x
        sta sp_
        lda pool_hi,x
        sta sp_+1
        ldy #4
        pla
        sta (sp_),y
        dey
        pla
        sta (sp_),y
        stz _pool_n
@r:     rts

; paint the cells of the lists pl_x / pl_y (pl_n of them), each once a
; frame (mark[] == stamp)
_paint_list:
        lda _pl_x
        sta lx
        lda _pl_x+1
        sta lx+1
        lda _pl_y
        sta ly
        lda _pl_y+1
        sta ly+1
        lda _pl_n
        sta cnt
        stz idx
@lp:    ldy idx
        cpy cnt
        beq @done
        lda (ly),y
        sta cy
        lda (lx),y
        sta cx
        inc idx
        ldx cy
        lda mark_lo,x
        sta rp
        lda mark_hi,x
        sta rp+1
        ldy cx
        lda (rp),y
        cmp _stamp
        beq @lp
        lda _stamp
        sta (rp),y
        inc _rd_cells
        jsr cell
        bra @lp
@done:  jmp _pool_flush

; every cell of the room that isn't empty (after a clear)
_paint_all:
        stz cy
@row:   stz cx
@col:   ldx cy
        lda room_lo,x
        sta rp
        lda room_hi,x
        sta rp+1
        ldy cx
        lda (rp),y
        tax
        lda _tile_frame,x
        beq @skip
        jsr cell
@skip:  inc cx
        lda cx
        cmp #20
        bne @col
        inc cy
        lda cy
        cmp #16
        bne @row
        jmp _pool_flush

pc_off: rts

; the cells under the screen rectangle ox, oy (int16, oy 6..101), ow x oh
; into dlx / dly at dln (dfull = 1 when the list is full)
_put_cells:
        clc                     ; x1 = ox + ow - 1
        lda _ox
        adc _ow
        sta x1
        lda _ox+1
        adc #0
        sta x1+1
        lda x1
        bne :+
        dec x1+1
:       dec x1
        lda x1+1
        bmi pc_off              ; all left of the screen
        lda _ox+1
        bmi @c0                 ; starts left of it
        bne pc_off              ; right of it
        lda _ox
        cmp #160
        bcs pc_off
        lsr a
        lsr a
        lsr a
        bra @c0s
@c0:    lda #0
@c0s:   sta cx0
        lda x1+1
        bne @c1m
        lda x1
        cmp #160
        bcs @c1m
        lsr a
        lsr a
        lsr a
        bra @c1s
@c1m:   lda #19
@c1s:   sta cx1
        lda _oy                 ; rows: oy is on screen (6..101)
        sec
        sbc #PF_Y
        tax
        lda _row6,x
        sta cy
        lda _oy
        clc
        adc _oh                 ; y1 + 1 = oy + oh
        sec
        sbc #PF_Y + 1
        cmp #96
        bcc :+
        lda #95
:       tax
        lda _row6,x
        sta cy1
        lda _dlx
        sta lx
        lda _dlx+1
        sta lx+1
        lda _dly
        sta ly
        lda _dly+1
        sta ly+1
@ry:    lda cx0
        sta cx
@rx:    ldy _dln
        cpy #DL_MAX
        bcs @full
        lda cx
        sta (lx),y
        lda cy
        sta (ly),y
        inc _dln
        lda cx
        cmp cx1
        beq @ny
        inc cx
        bra @rx
@ny:    lda cy
        cmp cy1
        beq @ret
        inc cy
        bra @ry
@full:  lda #1
        sta _dfull
@ret:   rts

; the sparks (draw_particles): a pixel each, its SCB (spark_s[i], a 1 x 1
; box) into the chain (olist[on]) and its cell into the repaint list
        .export _draw_sparks
        .import _p_on, _p_x, _p_y, _p_color, _spark_s, _olist, _on, _sy_tab

NSPARK = 24                     ; MAX_PARTICLES

        .rodata
spk_lo: .repeat NSPARK, i
        .byte <(_spark_s + i * SCB)
        .endrepeat
spk_hi: .repeat NSPARK, i
        .byte >(_spark_s + i * SCB)
        .endrepeat

        .bss
si:     .res 1
sx:     .res 1
sy:     .res 1

        .code
_draw_sparks:
        lda _dlx
        sta lx
        lda _dlx+1
        sta lx+1
        lda _dly
        sta ly
        lda _dly+1
        sta ly+1
        ldx #0
        bra @lp
@n1:    jmp @next
@lp:    stx si
        lda _p_on,x
        beq @n1
        txa
        asl a
        tay                     ; 2 * i: the int16 arrays
        lda _p_y+1,y            ; y (12.4) -> pixels: 0..255 or not drawn
        bmi @n1
        cmp #16
        bcs @n1
        asl a
        asl a
        asl a
        asl a
        sta sy
        lda _p_y,y
        lsr a
        lsr a
        lsr a
        lsr a
        ora sy
        tax
        lda _sy_tab,x
        sta sy
        lda _p_x+1,y            ; x (12.4) / 2 -> screen 0..159 or not drawn
        bmi @n1
        cmp #20
        bcs @n1
        asl a
        asl a
        asl a
        sta sx
        lda _p_x,y
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a
        ora sx
        sta sx
        ldx si
        lda spk_lo,x
        sta sp_
        lda spk_hi,x
        sta sp_+1
        ldy #7                  ; hpos
        lda sx
        sta (sp_),y
        iny
        lda #0
        sta (sp_),y
        iny                     ; vpos
        lda sy
        sta (sp_),y
        iny
        lda #0
        sta (sp_),y
        ldy #15                 ; penpal[0]: pen 1 -> the spark's colour
        lda _p_color,x
        sta (sp_),y
        lda _on                 ; olist[on++] = it
        asl a
        tay
        lda sp_
        sta _olist,y
        lda sp_+1
        sta _olist+1,y
        inc _on
        ldy _dln                ; its cell
        cpy #DL_MAX
        bcs @full
        lda sx
        lsr a
        lsr a
        lsr a
        sta (lx),y
        lda sy
        sec
        sbc #PF_Y
        tax
        lda _row6,x
        sta (ly),y
        inc _dln
@next:  ldx si
        inx
        cpx #NSPARK
        beq @done
        jmp @lp
@done:  rts
@full:  lda #1
        sta _dfull
        bra @next
