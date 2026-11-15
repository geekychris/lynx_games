; Lunar Rider (Lynx) - the terrain generator's loop (the original's
; generate_terrain_chunk(), game.c) in 6502.
;
; In cc65's C one pixel took ~1,500 cycles (the random number alone 330),
; a chunk of 64 a whole frame; here a pixel is ~100-150, a crater ~600.
;
; void __fastcall__ gen_steps(uint8_t n): up to n steps of the loop over the
; chunk gen_x..gen_end (ring positions; gen_wx = world x of gen_x): each
; step makes the pixel at gen_x flat ground, draws a random number 0..99
; and may start a feature there (with the original's thresholds, which
; rise with the difficulty): a small crater (16 pixels, 20 deep), a large
; one (28, 30 deep), a rock, a hill (24, the parabolic bump) or a mine.
; A feature is cut at the chunk's end, the loop goes on after its full
; width (the original's x += 15 / 27 / 23). Each feature is also added to
; the feature list (game.h) for drawing.
;
; rnd8: the SDK's xorshift16 (7, 9, 8) on the same rnd_state, inline.

        .setcpu "65SC02"
        .importzp ptr1, ptr2
        .import _terrain, _terrain_h, _difficulty, _rnd_state
        .import _gen_x, _gen_end, _gen_wx, _gen_busy, _gen_open
        .import _feat_wx_lo, _feat_wx_hi, _feat_ring_lo, _feat_ring_hi
        .import _feat_type, _feat_w, _feat_head, _feat_tail
        .export _gen_steps, _pct, _rnd8_fast

GROUND_Y = 200
T_FLAT   = 0
T_CR_SM  = 1
T_CR_LG  = 2
T_ROCK   = 3
T_HILL   = 4
T_MINE   = 5
CLEAR_TO = 80 + 24 + 30         ; gen_open: world x up to here stays clear

        .rodata
; rng() % 100 as pct[rnd8()]: i * 100 / 256
_pct:   .repeat 256, I
        .byte (I * 100) / 256
        .endrepeat
; the hill's bump: 8 - (hw * hw / 18), hw = w - 12, >= 0
bump:   .byte 0, 2, 3, 4, 5, 6, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 6, 6, 5, 4, 3, 2

        .bss
cnt:    .res 1
r:      .res 1
n:      .res 1                  ; how far the loop moves on
room:   .res 1                  ; pixels left in the chunk (max 255)
fw:     .res 1                  ; the feature's width (cut)
ftype:  .res 1
fh:     .res 1
t1:     .res 1                  ; the thresholds
t2:     .res 1
t3:     .res 1
t4:     .res 1
t5:     .res 1

        .code

; uint8_t rnd8_fast(void): the next xorshift16 value's low byte
_rnd8_fast:
        lda     _rnd_state
        lsr     a                       ; x ^= x << 7
        eor     _rnd_state+1
        sta     _rnd_state+1
        lda     #0
        ror     a
        eor     _rnd_state
        sta     _rnd_state
        lda     _rnd_state+1            ; x ^= x >> 9
        lsr     a
        eor     _rnd_state
        sta     _rnd_state
        eor     _rnd_state+1            ; x ^= x << 8
        sta     _rnd_state+1
        lda     _rnd_state
        ldx     #0
        rts

.proc _gen_steps
        sta     cnt
        ; thresholds: 3 + d, 5 + d, 8 + 2d, 12 + d, 14 + 2d
        lda     _difficulty
        clc
        adc     #3
        sta     t1
        adc     #2
        sta     t2
        adc     #7
        sta     t4
        lda     _difficulty
        asl     a
        adc     #8
        sta     t3
        adc     #6
        sta     t5
loop:   lda     _gen_busy
        beq     done
        lda     cnt
        beq     done
        dec     cnt
        jsr     step
        bra     loop
done:   rts
.endproc

.proc step
        clc
        lda     _gen_x
        adc     #<_terrain
        sta     ptr1
        lda     _gen_x+1
        adc     #>_terrain
        sta     ptr1+1
        clc
        lda     _gen_x
        adc     #<_terrain_h
        sta     ptr2
        lda     _gen_x+1
        adc     #>_terrain_h
        sta     ptr2+1
        lda     #T_FLAT
        sta     (ptr1)
        lda     #GROUND_Y
        sta     (ptr2)
        lda     #1
        sta     n
        jsr     _rnd8_fast
        tax
        lda     _pct,x
        sta     r

        ; port: original bug - the original kept world x 0..30 clear
        ; ("leave initial area clear"), but the buggy stands at x 80..104:
        ; a game could start over a crater or a few pixels from a rock. The
        ; area up to 30 pixels beyond the buggy's front is kept clear instead.
        lda     _gen_open
        beq     features
        lda     _gen_wx+1
        bne     features
        lda     _gen_wx
        cmp     #CLEAR_TO + 1
        bcs     features
        jmp     advance
features:
        sec                             ; room = min(255, gen_end - gen_x)
        lda     _gen_end
        sbc     _gen_x
        sta     room
        lda     _gen_end+1
        sbc     _gen_x+1
        beq     :+
        lda     #255
        sta     room
:       lda     r
        cmp     t1
        bcc     crater_sm
        cmp     t2
        bcc     crater_lg
        cmp     t3
        bcc     rock
        cmp     t4
        bcc     hill
        cmp     t5
        bcc     :+
        jmp     advance
:       lda     #T_MINE                 ; a ground mine
        bra     single
rock:   lda     #T_ROCK                 ; a rock / boulder
single: sta     (ptr1)
        sta     ftype
        lda     #1
        sta     fw
        jsr     feat_add
        bra     advance

crater_sm:
        lda     #T_CR_SM
        ldx     #16
        ldy     #GROUND_Y + 20
        bra     fill
crater_lg:
        lda     #T_CR_LG
        ldx     #28
        ldy     #GROUND_Y + 30
fill:   sta     ftype
        sty     fh
        stx     n
        cpx     room                    ; fw = min(width, room)
        bcc     :+
        ldx     room
:       stx     fw
        ldy     #0
:       lda     ftype
        sta     (ptr1),y
        lda     fh
        sta     (ptr2),y
        iny
        cpy     fw
        bne     :-
        jsr     feat_add
        bra     advance

hill:   lda     #T_HILL
        sta     ftype
        ldx     #24
        stx     n
        cpx     room
        bcc     :+
        ldx     room
:       stx     fw
        ldy     #0
:       lda     #T_HILL
        sta     (ptr1),y
        lda     #GROUND_Y
        sec
        sbc     bump,y
        sta     (ptr2),y
        iny
        cpy     fw
        bne     :-
        jsr     feat_add

advance:                                ; gen_x += n, gen_wx += n
        clc
        lda     _gen_x
        adc     n
        sta     _gen_x
        bcc     :+
        inc     _gen_x+1
:       clc
        lda     _gen_wx
        adc     n
        sta     _gen_wx
        bcc     :+
        inc     _gen_wx+1
:       lda     _gen_x                  ; done when gen_x >= gen_end
        cmp     _gen_end
        lda     _gen_x+1
        sbc     _gen_end+1
        bcc     :+
        stz     _gen_busy
:       rts
.endproc

; add (ftype, fw) at gen_x / gen_wx to the feature list (if not full)
.proc feat_add
        ldx     _feat_tail
        inx
        cpx     _feat_head
        beq     full
        dex
        lda     _gen_wx
        sta     _feat_wx_lo,x
        lda     _gen_wx+1
        sta     _feat_wx_hi,x
        lda     _gen_x
        sta     _feat_ring_lo,x
        lda     _gen_x+1
        sta     _feat_ring_hi,x
        lda     ftype
        sta     _feat_type,x
        lda     fw
        sta     _feat_w,x
        inc     _feat_tail
full:   rts
.endproc
