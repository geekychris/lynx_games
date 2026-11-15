; Nova Defense (Lynx) - the per-frame loops over the sprites, in 6502.
;
; In cc65's C, writing the 48 aliens' SCBs took ~5,500 cycles a frame and
; the erase pass's two flag loops over 62 SCBs ~3,000: a tenth of a 50 Hz
; frame. Here they are a few hundred each.

        .setcpu "65SC02"
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4
        .import _cs, _alive_mask, _grid_x, _grid_y, _anim_frame
        .import _alien_frames, _col_x, _row_y
        .export _aliens_scbs, _reusepal_on, _reusepal_off

SCB_SIZE   = 23                     ; SCB_REHV_PAL
SCB_CTL1   = 1
SCB_DATA   = 5
SCB_HPOS   = 7
SCB_VPOS   = 9
SKIP       = $04
REUSEPAL   = $08
S_ALIEN    = 3                      ; main.c: the SCBs of a set
N_ALIENS   = 48
N_SCB      = 65
ALIEN_SHIFT = 12

        .bss
row:    .res 1
x0:     .res 2
f0:     .res 2
f1:     .res 2
col:    .res 1

        .code

; ptr1 = &cs[S_ALIEN]
.macro FIRST_ALIEN
        clc
        lda     _cs
        adc     #<(S_ALIEN * SCB_SIZE)
        sta     ptr1
        lda     _cs+1
        adc     #>(S_ALIEN * SCB_SIZE)
        sta     ptr1+1
.endmacro

.macro NEXT_SCB
        clc
        lda     ptr1
        adc     #SCB_SIZE
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:
.endmacro

; void reusepal_on(void) / reusepal_off(void): set / clear REUSEPAL in every
; SCB of the current set from the aliens on (the erase pass)
.proc _reusepal_on
        FIRST_ALIEN
        ldx     #N_SCB - S_ALIEN
loop:   ldy     #SCB_CTL1
        lda     (ptr1),y
        ora     #REUSEPAL
        sta     (ptr1),y
        NEXT_SCB
        dex
        bne     loop
        rts
.endproc

.proc _reusepal_off
        FIRST_ALIEN
        ldx     #N_SCB - S_ALIEN
loop:   ldy     #SCB_CTL1
        lda     (ptr1),y
        and     #<~REUSEPAL
        sta     (ptr1),y
        NEXT_SCB
        dex
        bne     loop
        rts
.endproc

; void aliens_scbs(void): the 48 aliens' SCBs of the current set. An alive
; alien: shown, at ((grid_x + row shift + c * 26) / 2, (grid_y + r * 14) / 2),
; picture alien_frames[type * 2 + (anim_frame ^ (c & 1))] (neighbouring
; columns flap out of step, as the original); a dead one: SKIP.
.proc _aliens_scbs
        FIRST_ALIEN
        stz     row
rowloop:
        ldx     row
        lda     _alive_mask,x
        sta     tmp1                ; the row's alive bits
        ; y = (grid_y + row_y[r]) >> 1 (0..101)
        clc
        lda     _grid_y
        adc     _row_y,x
        sta     tmp2
        lda     _grid_y+1
        adc     #0
        lsr     a
        ror     tmp2                ; tmp2 = vpos (low byte; high is 0)
        ; x0 = grid_x + (odd row ? 12 : 0)
        txa
        and     #1
        beq     :+
        lda     #ALIEN_SHIFT
:       clc
        adc     _grid_x
        sta     x0
        lda     _grid_x+1
        adc     #0
        sta     x0+1
        ; frames: type = r / 2 -> index (r & ~1) + anim_frame, its partner ^1
        txa
        and     #$FE
        clc
        adc     _anim_frame
        asl     a
        tay
        lda     _alien_frames,y
        sta     f0
        lda     _alien_frames+1,y
        sta     f0+1
        tya
        eor     #2                  ; the other frame of the pair
        tay
        lda     _alien_frames,y
        sta     f1
        lda     _alien_frames+1,y
        sta     f1+1
        stz     col
colloop:
        ldy     #SCB_CTL1
        lsr     tmp1
        bcs     alive
        lda     (ptr1),y
        ora     #SKIP
        sta     (ptr1),y
        bra     next
alive:  lda     (ptr1),y
        and     #<~SKIP
        sta     (ptr1),y
        ldx     col
        ldy     #SCB_HPOS
        clc
        lda     x0
        adc     _col_x,x
        sta     tmp3
        lda     x0+1
        adc     #0
        lsr     a
        sta     tmp4
        lda     tmp3
        ror     a
        sta     (ptr1),y
        iny
        lda     tmp4
        sta     (ptr1),y
        iny                         ; SCB_VPOS
        lda     tmp2
        sta     (ptr1),y
        iny
        lda     #0
        sta     (ptr1),y
        ldy     #SCB_DATA
        txa
        lsr     a
        bcs     odd
        lda     f0
        sta     (ptr1),y
        iny
        lda     f0+1
        sta     (ptr1),y
        bra     next
odd:    lda     f1
        sta     (ptr1),y
        iny
        lda     f1+1
        sta     (ptr1),y
next:   NEXT_SCB
        inc     col
        lda     col
        cmp     #8
        bne     colloop
        inc     row
        lda     row
        cmp     #6
        beq     done
        jmp     rowloop
done:   rts
.endproc
