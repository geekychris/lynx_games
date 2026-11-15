; roomasm.s - the room's shapes into Suzy SCBs (tools/gen_data.py makes
; them; room.c draws the rest):
;   1 x y w h pen                      a box
;   2 y h xt wt dx dw pen              a trapezoid (8.8: its left edge and
;                                      width at the top, and their change a
;                                      row: Suzy's tilt and stretch)
;   6 x y h pen n data[n]              a 1-bpp row stretched down (the back
;                                      wall's vertical grid lines)
; room_shapes(p) turns shapes into SCBs from a pool of 16 and draws them,
; up to the first op that isn't one of these (a line, a shadow mark, the
; end), and returns its address. In C this cost ~1.7k cycles a shape.

        .export _room_shapes
        .import _spr_draw, _spr_pixel_data
        .importzp ptr1, ptr2, ptr3

NPOOL = 16
SCBSZ = 27                      ; SCB_REHVST_PAL

.segment "BSS"
pool:   .res    NPOOL * SCBSZ
npool:  .res    1

.segment "RODATA"
poollo:
.repeat NPOOL, I
        .byte   <(pool + I * SCBSZ)
.endrepeat
poolhi:
.repeat NPOOL, I
        .byte   >(pool + I * SCBSZ)
.endrepeat

.segment "CODE"

; const uint8_t *room_shapes(const uint8_t *p)
_room_shapes:
        sta     ptr1
        stx     ptr1+1
@op:    lda     (ptr1)
        cmp     #1
        beq     @box
        cmp     #2
        beq     @trap
        cmp     #6
        bne     :+
        jmp     @rowpat
:       jsr     flush
        lda     ptr1
        ldx     ptr1+1
        rts

@box:   jsr     newscb          ; 1 x y w h pen
        ldy     #1
        lda     (ptr1),y        ; x
        ldy     #7
        sta     (ptr2),y
        iny
        lda     #0
        sta     (ptr2),y
        ldy     #2
        lda     (ptr1),y        ; y
        ldy     #9
        sta     (ptr2),y
        iny
        lda     #0
        sta     (ptr2),y
        iny                     ; 11: hsize = w << 8
        sta     (ptr2),y
        ldy     #3
        lda     (ptr1),y
        ldy     #12
        sta     (ptr2),y
        iny                     ; 13: vsize = h << 8
        lda     #0
        sta     (ptr2),y
        ldy     #4
        lda     (ptr1),y
        ldy     #14
        sta     (ptr2),y
        ldy     #5
        lda     (ptr1),y        ; pen
        ldy     #19
        sta     (ptr2),y
        lda     #6
        bra     @next

@trap:  jsr     newscb          ; 2 y h xt.lo xt.hi wt.lo wt.hi dx.lo dx.hi dw.lo dw.hi pen
        ldy     #3
        lda     (ptr1),y        ; hpos = (xt + $80) >> 8, sign-extended
        clc
        adc     #$80
        iny
        lda     (ptr1),y
        adc     #0
        ldy     #7
        sta     (ptr2),y
        iny
        ldx     #0
        cmp     #$80
        bcc     :+
        dex
:       txa
        sta     (ptr2),y
        ldy     #1
        lda     (ptr1),y        ; y
        ldy     #9
        sta     (ptr2),y
        iny
        lda     #0
        sta     (ptr2),y
        ldy     #5              ; hsize = wt
        lda     (ptr1),y
        ldy     #11
        sta     (ptr2),y
        ldy     #6
        lda     (ptr1),y
        ldy     #12
        sta     (ptr2),y
        iny                     ; 13: vsize = h << 8
        lda     #0
        sta     (ptr2),y
        ldy     #2
        lda     (ptr1),y
        ldy     #14
        sta     (ptr2),y
        ldy     #9              ; stretch = dw
        lda     (ptr1),y
        ldy     #15
        sta     (ptr2),y
        ldy     #10
        lda     (ptr1),y
        ldy     #16
        sta     (ptr2),y
        ldy     #7              ; tilt = dx
        lda     (ptr1),y
        ldy     #17
        sta     (ptr2),y
        ldy     #8
        lda     (ptr1),y
        ldy     #18
        sta     (ptr2),y
        ldy     #11
        lda     (ptr1),y        ; pen
        ldy     #19
        sta     (ptr2),y
        lda     #12
@next:  clc
        adc     ptr1
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       jmp     @op

@rowpat:
        jsr     newscb          ; 6 x y h pen n data[n]
        ldy     #0
        lda     #$04            ; BPP_1 | TYPE_NORMAL
        sta     (ptr2),y
        iny
        lda     #$B0            ; LITERAL | REHVST
        sta     (ptr2),y
        lda     ptr1            ; data = p + 6
        clc
        adc     #6
        ldy     #5
        sta     (ptr2),y
        lda     ptr1+1
        adc     #0
        iny
        sta     (ptr2),y
        ldy     #1
        lda     (ptr1),y        ; x
        ldy     #7
        sta     (ptr2),y
        iny
        lda     #0
        sta     (ptr2),y
        ldy     #2
        lda     (ptr1),y        ; y
        ldy     #9
        sta     (ptr2),y
        iny
        lda     #0
        sta     (ptr2),y
        iny                     ; 11: hsize = 1.0
        sta     (ptr2),y
        iny
        lda     #1
        sta     (ptr2),y
        iny                     ; 13: vsize = h << 8
        lda     #0
        sta     (ptr2),y
        ldy     #3
        lda     (ptr1),y
        ldy     #14
        sta     (ptr2),y
        ldy     #4
        lda     (ptr1),y        ; pen
        ldy     #19
        sta     (ptr2),y
        ldy     #5
        lda     (ptr1),y        ; n
        clc
        adc     #6
        bra     @next

; ptr2 = the next SCB from the pool, linked from the one before, its common
; fields set (1-bpp packed pixel, BACKNONCOLL: colour 0 is drawn too)
newscb:
        ldx     npool
        cpx     #NPOOL
        bne     :+
        jsr     flush
        ldx     #0
:       lda     poollo,x
        sta     ptr2
        lda     poolhi,x
        sta     ptr2+1
        cpx     #0
        beq     @first
        lda     poollo-1,x      ; the one before: next = this
        sta     ptr3
        lda     poolhi-1,x
        sta     ptr3+1
        ldy     #3
        lda     ptr2
        sta     (ptr3),y
        iny
        lda     ptr2+1
        sta     (ptr3),y
@first: inc     npool
        ldy     #0
        lda     #$01            ; BPP_1 | TYPE_BACKNONCOLL
        sta     (ptr2),y
        iny
        lda     #$30            ; PACKED | REHVST
        sta     (ptr2),y
        iny
        lda     #$20            ; NO_COLLIDE
        sta     (ptr2),y
        iny
        lda     #0
        sta     (ptr2),y        ; next = 0
        iny
        sta     (ptr2),y
        iny
        lda     #<_spr_pixel_data
        sta     (ptr2),y
        iny
        lda     #>_spr_pixel_data
        sta     (ptr2),y
        ldy     #15             ; stretch, tilt, the pen palette: 0
        lda     #0
:       sta     (ptr2),y
        iny
        cpy     #SCBSZ
        bne     :-
        rts

flush:                          ; (TGI uses ptr1: keep ours)
        lda     npool
        beq     :+
        lda     ptr1
        pha
        lda     ptr1+1
        pha
        lda     #<pool
        ldx     #>pool
        jsr     _spr_draw
        stz     npool
        pla
        sta     ptr1+1
        pla
        sta     ptr1
:       rts
