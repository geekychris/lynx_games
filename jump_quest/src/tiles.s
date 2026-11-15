; Jump Quest (Lynx) - the frame's sprite chain (main.c).
;
; The tiles. The level's non-sky tiles have SCB_RENONEs of their own (tscb,
; made by main.c rows_index() when a level starts: one a tile, or one for a
; run of 2-4 identical ground, grass, brick, stone or platform tiles), row
; by row in column order, at their places in the whole level (x = 8 *
; column, y = 7 * row). Suzy's HOFF register (the screen's left edge)
; scrolls them, so a frame only has to link the ones in view: in each row
; they are one run of the row's SCBs (tiles_span finds it with a cursor
; that follows the view, in each row's list of its SCBs' last columns,
; row_list), and tiles_link joins those runs into a chain. (Visiting all
; 290 cells in view and writing an SCB for each tile was a fifth of a
; frame; this is a few thousand cycles.)
;
; build_items: the active items and (unless bo_noenemies) the live enemies;
; build_parts: the live particles. Screen x = (x - view_x) / 2 (+ HOFF in
; the SCB), screen y = y * 7 / 16; sprites wholly off the playfield are
; left out.

        .setcpu "65SC02"
        .export _tiles_span, _tiles_link, _build_items, _build_parts
        .export _bt_scb, _bt_col, _bt_x, _bo_noenemies, _bt_hoff
        .export _row_list, _row_cur, _rowl_lo := row_lo, _rowl_hi := row_hi, _span_a, _span_b, _lk_set, _lk_head, _lk_tail
        .export _row_addr_lo, _row_addr_hi, _tiles_unlinked
        .import _n_tiles, _art_item, _art_enemy, _art_dot
        .import _game, _view_x, _jq_item_bob
        .importzp ptr1, ptr2, ptr3

ROW_STRIDE = 152                ; (mul11 table: up to 151 sprites a row)
SCB_SIZE  = 11                  ; SCB_RENONE
SCB_NEXT  = 3
SCB_DATA  = 5
CTL0      = $C4                 ; BPP_4 | TYPE_NORMAL (clears a flip left by the hero)
ENEMIES   = 56                  ; offsetof(Game, ...) (checked in jq.c)
ITEMS     = 56 + 16 * 13
PARTICLES = ITEMS + 24 * 9
HUD_Y     = 96

        .bss
_bt_scb:   .res 2
_bt_col:   .res 1
_bt_x:     .res 1
_bt_hoff:  .res 2
_span_a:   .res 28
_span_b:   .res 28
_lk_set:   .res 1
_lk_head:  .res 2
_lk_tail:  .res 2
_row_addr_lo: .res 14
_row_addr_hi: .res 14
nends:  .res 1
ends_lo: .res 14
ends_hi: .res 14
_bo_noenemies: .res 1
_row_list: .res 160 + 14
row_lo: .res 14
row_hi: .res 14
_row_cur:  .res 14
row:    .res 1
ypos:   .res 1
cend:   .res 1
ex:     .res 2              ; emit: x, y, data
ey:     .res 2
ed:     .res 2
cnt:    .res 1

        .rodata
mul11_lo:
        .repeat ROW_STRIDE, I
        .byte <(I * 11)
        .endrep
mul11_hi:
        .repeat ROW_STRIDE, I
        .byte >(I * 11)
        .endrep

        .code
; a new level: its tile SCBs are new, nothing to put back
.proc _tiles_unlinked
        stz     nends
        rts
.endproc

; write one SCB at ptr2 (data ed, x ex, y ey) and move ptr2 on
.proc put_scb
        lda     #CTL0
        sta     (ptr2)
        ldy     #SCB_DATA
        lda     ed
        sta     (ptr2),y
        iny
        lda     ed+1
        sta     (ptr2),y
        iny
        clc
        lda     ex
        adc     _bt_hoff
        sta     (ptr2),y
        iny
        lda     ex+1
        adc     _bt_hoff+1
        sta     (ptr2),y
        iny
        lda     ey
        sta     (ptr2),y
        iny
        lda     ey+1
        sta     (ptr2),y
        ldy     #SCB_NEXT
        clc
        lda     ptr2
        adc     #SCB_SIZE
        sta     (ptr2),y
        tax
        iny
        lda     ptr2+1
        adc     #0
        sta     (ptr2),y
        sta     ptr2+1
        stx     ptr2
        rts
.endproc

; tiles_span: span_a/span_b[set] = the run of each row's list in view
; (columns bt_col .. bt_col + 20, + 21 when the view is between tiles)
.proc _tiles_span
        lda     _bt_x
        cmp     #1                      ; C = (bt_x != 0)
        lda     #20 + 3                 ; + 3: the list has runs' last columns (up to 4 wide)
        adc     _bt_col
        sta     cend
        lda     _lk_set                 ; the set's spans: + 14 * set
        beq     :+
        lda     #14
:       sta     ypos                    ; (offset)
        stz     row
        stz     _n_tiles
rowloop:
        ldx     row
        lda     row_lo,x
        sta     ptr1
        lda     row_hi,x
        sta     ptr1+1
        ldy     _row_cur,x
back:   cpy     #0
        beq     fwd
        dey
        lda     (ptr1),y
        cmp     _bt_col
        bcs     back
        iny
fwd:    lda     (ptr1),y
        cmp     _bt_col
        bcs     :+
        iny
        bra     fwd
:       tya
        sta     _row_cur,x
        txa
        clc
        adc     ypos
        tax
        tya
        sta     _span_a,x
end:    lda     (ptr1),y
        cmp     cend
        bcs     :+
        iny
        bra     end
:       tya
        sta     _span_b,x
        sec                             ; n_tiles += b - a
        sbc     _span_a,x
        clc
        adc     _n_tiles
        sta     _n_tiles
        inc     row
        lda     row
        cmp     #14
        bcc     rowloop
        rts
.endproc

; tiles_link: puts back the links the last call changed, then chains
; lk_head -> each row's run in view (span_a/span_b[lk_set]) -> lk_tail
.proc _tiles_link
        ldx     nends                   ; restore: end->next = end + 11
        beq     link
rest:   dex
        lda     ends_lo,x
        sta     ptr1
        lda     ends_hi,x
        sta     ptr1+1
        ldy     #SCB_NEXT
        clc
        lda     ptr1
        adc     #SCB_SIZE
        sta     (ptr1),y
        iny
        lda     ptr1+1
        adc     #0
        sta     (ptr1),y
        txa
        bne     rest
link:   stz     nends
        lda     _lk_head
        sta     ptr2                    ; prev
        lda     _lk_head+1
        sta     ptr2+1
        lda     _lk_set
        beq     :+
        lda     #14
:       sta     ypos
        stz     row
lrow:   lda     row
        clc
        adc     ypos
        tax
        lda     _span_a,x
        cmp     _span_b,x
        bcs     lnext                   ; empty
        ; prev->next = row start + 11 * a
        tay
        ldx     row
        clc
        lda     mul11_lo,y
        adc     _row_addr_lo,x
        sta     ptr1
        lda     mul11_hi,y
        adc     _row_addr_hi,x
        sta     ptr1+1
        ldy     #SCB_NEXT
        lda     ptr1
        sta     (ptr2),y
        iny
        lda     ptr1+1
        sta     (ptr2),y
        ; prev = row start + 11 * (b - 1)
        lda     row
        clc
        adc     ypos
        tax
        ldy     _span_b,x
        dey
        ldx     row
        clc
        lda     mul11_lo,y
        adc     _row_addr_lo,x
        sta     ptr2
        lda     mul11_hi,y
        adc     _row_addr_hi,x
        sta     ptr2+1
        ldx     nends
        lda     ptr2
        sta     ends_lo,x
        lda     ptr2+1
        sta     ends_hi,x
        inc     nends
lnext:  inc     row
        lda     row
        cmp     #14
        bcc     lrow
        ldy     #SCB_NEXT               ; the last -> lk_tail
        lda     _lk_tail
        sta     (ptr2),y
        iny
        lda     _lk_tail+1
        sta     (ptr2),y
        rts
.endproc

; ex = (word at (ptr1),y - view_x) >> 1 (signed); Y += 2
.proc set_x
        sec
        lda     (ptr1),y
        sbc     _view_x
        sta     ex
        iny
        lda     (ptr1),y
        sbc     _view_x+1
        cmp     #$80                    ; arithmetic shift right
        ror     a
        sta     ex+1
        ror     ex
        iny
        rts
.endproc

; ey = (A/X = world y) * 7 / 16 (signed)
.proc set_y
        sta     ey
        stx     ey+1
        ; ptr: t = y * 8
        sta     ed                      ; (ed is free until the picture is set)
        stx     ed+1
        asl     ed
        rol     ed+1
        asl     ed
        rol     ed+1
        asl     ed
        rol     ed+1
        sec                             ; t - y
        lda     ed
        sbc     ey
        sta     ey
        lda     ed+1
        sbc     ey+1
        ldx     #4                      ; >> 4, signed
:       cmp     #$80
        ror     a
        ror     ey
        dex
        bne     :-
        sta     ey+1
        rts
.endproc

; C set if a sprite at ex is wholly off the screen (x <= -8 or x >= 160);
; keeps Y
.proc x_off
        lda     ex+1
        beq     xpos
        cmp     #$FF
        bne     no
        lda     ex
        cmp     #$F9                    ; >= -7: on
        bcc     no
        clc
        rts
xpos:   lda     ex
        cmp     #160
        rts
no:     sec
        rts
.endproc

; C clear if (ex, ey) puts an 8 x 7 sprite on the playfield
; (-8 < x < 160, -14 < y < 96)
.proc on_screen
        lda     ex+1
        beq     xpos
        cmp     #$FF
        bne     no
        lda     ex
        cmp     #$F9                    ; >= -7
        bcc     no
        bra     xok
xpos:   lda     ex
        cmp     #160
        bcs     no
xok:    lda     ey+1
        beq     ypos_
        cmp     #$FF
        bne     no
        lda     ey
        cmp     #$F3                    ; >= -13
        bcc     no
        clc
        rts
ypos_:  lda     ey
        cmp     #HUD_Y
        rts                             ; C set: off (below the playfield)
no:     sec
        rts
.endproc

.proc _build_items
        lda     _bt_scb
        sta     ptr2
        lda     _bt_scb+1
        sta     ptr2+1
        ; items: type 0, x 1, y 3, active 5 (9 bytes)
        lda     #<(_game + ITEMS)
        sta     ptr1
        lda     #>(_game + ITEMS)
        sta     ptr1+1
        lda     #24
        sta     cnt
iloop:  ldy     #5
        lda     (ptr1),y
        beq     inext
        ldy     #1
        jsr     set_x
        jsr     x_off
        bcs     inext
        lda     _jq_item_bob            ; y + bob
        ldx     #0
        cmp     #$80
        bcc     :+
        dex
:       clc
        adc     (ptr1),y
        pha
        txa
        iny
        adc     (ptr1),y
        tax
        pla
        jsr     set_y
        jsr     on_screen
        bcs     inext
        lda     (ptr1)                  ; the type: art_item[type - 10]
        sec
        sbc     #10
        asl     a
        tax
        lda     _art_item,x
        sta     ed
        lda     _art_item+1,x
        sta     ed+1
        jsr     put_scb
inext:  clc
        lda     ptr1
        adc     #9
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       dec     cnt
        bne     iloop

        lda     _bo_noenemies
        bne     out
        ; enemies: type 0, x 1, y 3, alive 10, frame 11 (13 bytes)
        lda     #<(_game + ENEMIES)
        sta     ptr1
        lda     #>(_game + ENEMIES)
        sta     ptr1+1
        lda     #16
        sta     cnt
eloop:  ldy     #10
        lda     (ptr1),y
        beq     enext
        ldy     #1
        jsr     set_x
        jsr     x_off
        bcs     enext
        lda     (ptr1),y
        pha
        iny
        lda     (ptr1),y
        tax
        pla
        jsr     set_y
        jsr     on_screen
        bcs     enext
        lda     (ptr1)                  ; art_enemy[type - 2][frame]
        sec
        sbc     #2
        asl     a
        ldy     #11
        ora     (ptr1),y
        asl     a
        tax
        lda     _art_enemy,x
        sta     ed
        lda     _art_enemy+1,x
        sta     ed+1
        jsr     put_scb
enext:  clc
        lda     ptr1
        adc     #13
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       dec     cnt
        bne     eloop
out:    lda     ptr2
        sta     _bt_scb
        lda     ptr2+1
        sta     _bt_scb+1
        rts
.endproc

.proc _build_parts
        lda     _bt_scb
        sta     ptr2
        lda     _bt_scb+1
        sta     ptr2+1
        ; particles: x 0, y 2, life 8 (10 bytes)
        lda     #<(_game + PARTICLES)
        sta     ptr1
        lda     #>(_game + PARTICLES)
        sta     ptr1+1
        lda     #16
        sta     cnt
ploop:  ldy     #8
        lda     (ptr1),y
        beq     pnext
        bmi     pnext
        ldy     #0
        jsr     set_x
        jsr     x_off
        bcs     pnext
        lda     (ptr1),y
        pha
        iny
        lda     (ptr1),y
        tax
        pla
        jsr     set_y
        jsr     on_screen
        bcs     pnext
        lda     _art_dot
        sta     ed
        lda     _art_dot+1
        sta     ed+1
        jsr     put_scb
pnext:  clc
        lda     ptr1
        adc     #10
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       dec     cnt
        bne     ploop
        lda     ptr2
        sta     _bt_scb
        lda     ptr2+1
        sta     _bt_scb+1
        rts
.endproc
