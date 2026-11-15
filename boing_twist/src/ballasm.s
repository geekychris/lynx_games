; ballasm.s - the ball's hot loops: drawing it, a twisting band's rows,
; the flip.
;
; The texture (ball.c): texl = 4 | c (c: 0 the groove, 1-3 the picture's
; colours), rows of 96 (columns 64-95 repeat 0-31, so the turn is added to
; the row's address). The loops use sta/lda abs,x with addresses patched
; per line (self-modifying code: the Lynx's memory is all RAM).
;
; The flip's tables and its polar copy of the half live in the second
; screen buffer while playing (play is single-buffered: ball.h).

        .export _band_rotate, _flip_begin, _flip_frame, _view_play
        .export _tw_args, _fl_turn, _fl_ang, _tex_rows, _tx_args
        .import _texl, _vw_rows, _pic_buf
        .import _colslot, _fracu, _groove_u, _rowslot, _fracv, _groove_v
        .import _srcu, _srcv, _home_hb16, _home_hs16, _slot_u0, _cur_S
        .importzp ptr1, ptr2, ptr3, tmp1, tmp2, tmp3, tmp4

ROW = 96
FL_PBUILD = $E018               ; the flip file (cart/flip.bin): pbuild, then the rows
FL_ROWS   = $E018 + 2048
FL_P      = $ED00               ; the polar copy: 16 rings of 64 angles

.segment "DATA"
_tw_args:
tw_src:   .word 0
tw_v0:    .byte 0
tw_n:     .byte 0
tw_shift: .byte 0
_fl_turn: .byte 0
_fl_ang:  .byte 0

.segment "CODE"

; ptr2 = texl + A * 96 (A < 64)
rowaddr:
        sta     tmp1
        lda     #0
        sta     tmp2
        lda     tmp1            ; A * 32 into tmp1/tmp2 (16-bit), then * 3
        asl     a
        rol     tmp2
        asl     a
        rol     tmp2
        asl     a
        rol     tmp2
        asl     a
        rol     tmp2
        asl     a
        rol     tmp2            ; tmp2:A = v * 32
        sta     tmp3
        ldx     tmp2            ; x:tmp3 = v * 32
        asl     a
        rol     tmp2            ; tmp2:A = v * 64
        clc
        adc     tmp3
        sta     tmp3
        txa
        adc     tmp2
        tax                     ; x:tmp3 = v * 96
        lda     tmp3
        clc
        adc     #<_texl
        sta     ptr2
        txa
        adc     #>_texl
        sta     ptr2+1
        rts

; ------------------------------------------------------------ band_rotate
; for each of n rows from v0: row[c] = src[(c + shift) & 63], c < 96
_band_rotate:
        lda     tw_src
        sta     ptr1
        lda     tw_src+1
        sta     ptr1+1
        lda     tw_n
        beq     @done
        sta     @left
        lda     tw_v0
        sta     @v
@row:   lda     @v
        jsr     rowaddr
        lda     ptr2
        sta     @l+1
        lda     ptr2+1
        sta     @l+2
        ldy     tw_shift
        ldx     #0
@c:     lda     (ptr1),y
@l:     sta     $FFFF,x
        iny
        tya
        and     #63
        tay
        inx
        cpx     #ROW
        bne     @c
        lda     ptr1            ; the next saved row
        clc
        adc     #64
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       inc     @v
        dec     @left
        bne     @row
@done:  rts
@v:     .byte 0
@left:  .byte 0

; ------------------------------------------------------------ flip_begin
; fl_p[r * 64 + a] = texl[pbuild[r * 64 + a] + turn]
_flip_begin:
        lda     #<FL_PBUILD
        sta     ptr1
        lda     #>FL_PBUILD
        sta     ptr1+1
        lda     #<FL_P
        sta     ptr2
        lda     #>FL_P
        sta     ptr2+1
        lda     #16
        sta     tmp2            ; rings left
@ring:  lda     #0
        sta     tmp1            ; a
@ang:   ldy     #0
        lda     (ptr1),y
        clc
        adc     _fl_turn
        tax
        iny
        lda     (ptr1),y
        adc     #0
        sta     tmp3
        txa
        clc
        adc     #<_texl
        sta     ptr3
        lda     tmp3
        adc     #>_texl
        sta     ptr3+1
        lda     (ptr3)
        ldy     tmp1
        sta     (ptr2),y
        lda     ptr1
        clc
        adc     #2
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       inc     tmp1
        lda     tmp1
        cmp     #64
        bne     @ang
        lda     ptr2
        clc
        adc     #64
        sta     ptr2
        bcc     :+
        inc     ptr2+1
:       dec     tmp2
        bne     @ring
        rts

; ------------------------------------------------------------ flip_frame
; per window row (fl_rows): off, n, r[n], a[n]: texl[off + turn + k] =
; fl_p[r * 64 + (a + yth) & 63] where yth = (64 - ang) & 63
_flip_frame:
        lda     #<FL_ROWS
        sta     ptr1
        lda     #>FL_ROWS
        sta     ptr1+1
        lda     #64
        sec
        sbc     _fl_ang
        and     #63
        sta     @yth+1
        stz     ptr3            ; FL_P is page aligned: the ring's offset goes in Y
@row:   ldy     #1
        lda     (ptr1),y
        cmp     #$FF
        bne     :+
        rts
:       sta     tmp2            ; off high
        dey
        lda     (ptr1),y
        clc
        adc     _fl_turn
        bcc     :+
        inc     tmp2
:       clc
        adc     #<_texl
        sta     @l+1
        lda     tmp2
        adc     #>_texl
        sta     @l+2
        ldy     #2
        lda     (ptr1),y
        sta     @n+1
        lda     ptr1            ; r[] at ptr1 + 3, a[] after it
        clc
        adc     #3
        sta     @r+1
        lda     ptr1+1
        adc     #0
        sta     @r+2
        lda     @r+1
        clc
        adc     @n+1
        sta     @a+1
        lda     @r+2
        adc     #0
        sta     @a+2
        ldx     #0
@tex:
@r:     ldy     $FFFF,x
        lda     prow_hi,y
        sta     ptr3+1
        lda     prow_lo,y
        sta     tmp1
@a:     lda     $FFFF,x
        clc
@yth:   adc     #0
        and     #63
        ora     tmp1
        tay
        lda     (ptr3),y
@l:     sta     $FFFF,x
        inx
@n:     cpx     #0
        bne     @tex
        lda     @a+1            ; ptr1 = a[] + n: the next row
        clc
        adc     @n+1
        sta     ptr1
        lda     @a+2
        adc     #0
        sta     ptr1+1
        jmp     @row

.segment "RODATA"
prow_lo:
.repeat 16, I
        .byte   <(FL_P + I * 64)        ; ptr3's low byte stays 0: FL_P is page aligned
.endrepeat
prow_hi:
.repeat 16, I
        .byte   >(FL_P + I * 64)
.endrepeat
; c (0-3) -> 4 | c in both nibbles
dbl:    .byte   $44, $55, $66, $77

.segment "CODE"

; ------------------------------------------------------------ view_play
; The ball at turn A into ball_spr, from the tables tools/gen_data.py made
; (src/view.s): per sprite line its pairs of pixels, in runs that share a
; texture window (rows v0..v0+2) and a light step. A pair is one byte:
;     (texl[base + even[k]] & $F0 | texl[base + odd[k]] & $0F) ^ eor[step]
; (one fetch when both show the same texel), base = texl + v0 * 96 + turn.
; Texels are 4 | c in both nibbles; the EOR turns pen 4 | c into 8 | c (dark)
; or 12 | c (the highlight).
_view_play:
        sta     vp_turn
        lda     #<_vw_rows
        sta     ptr1
        lda     #>_vw_rows
        sta     ptr1+1
@line:  ldy     #1
        lda     (ptr1),y
        bne     :+
        rts                     ; a 0 address: the end
:       sta     @w+2
        sta     ptr3+1
        dey
        lda     (ptr1),y
        sta     @w+1
        sta     ptr3
        ldy     #2
        lda     (ptr1),y
        sta     @e+1
        iny
        lda     (ptr1),y
        sta     @e+2
        iny
        lda     (ptr1),y
        sta     @o+1
        iny
        lda     (ptr1),y
        sta     @o+2
        iny
        lda     (ptr1),y
        sta     vp_npairs
        iny
        lda     (ptr1),y
        sta     vp_masks
        iny
        lda     (ptr1),y
        sta     vp_nruns
        lda     ptr1
        clc
        adc     #9
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       ldx     #0
@run:   ldy     #1
        lda     (ptr1),y        ; v0
        tay
        lda     rowlo,y
        clc
        adc     vp_turn
        sta     ptr2
        lda     rowhi,y
        adc     #0
        sta     ptr2+1
        ldy     #2
        lda     (ptr1),y        ; the light step: its EOR, both nibbles
        tay
        lda     vp_eor,y
        sta     @x+1
        lda     (ptr1)          ; the run's end
        sta     @end+1
        lda     ptr1
        clc
        adc     #3
        sta     ptr1
        bcc     @pair
        inc     ptr1+1
@pair:
@e:     ldy     $FFFF,x
        lda     (ptr2),y        ; 4 | c in both nibbles
@o:     ldy     $FFFF,x
        cpy     #$FF
        beq     @x              ; both pixels show this texel
        and     #$F0
        sta     tmp1
        lda     (ptr2),y
        and     #$0F
        ora     tmp1
@x:     eor     #0
@w:     sta     $FFFF,x
        inx
@end:   cpx     #0
        bne     @pair
        dec     vp_nruns
        bne     @run
        ; a first / last byte half outside the ball: that pixel transparent
        lda     vp_masks
        lsr     a
        bcc     :+
        lda     (ptr3)
        and     #$0F
        sta     (ptr3)
:       lda     vp_masks
        and     #2
        beq     @next
        ldy     vp_npairs
        dey
        lda     (ptr3),y
        and     #$F0
        sta     (ptr3),y
@next:  jmp     @line

.segment "DATA"
vp_turn:   .byte 0
vp_npairs: .byte 0
vp_masks:  .byte 0
vp_nruns:  .byte 0

.segment "RODATA"
; texl + v * 96
rowlo:
.repeat 32, I
        .byte   <(_texl + I * ROW)
.endrepeat
rowhi:
.repeat 32, I
        .byte   >(_texl + I * ROW)
.endrepeat
; the light steps: pen 4 | c stays (lit), becomes 8 | c (dark) or 12 | c (the highlight)
vp_eor: .byte   $00, $CC, $88

; ------------------------------------------------------------ tex_rows
; Texture rows v0..v1-1 from the puzzle's slots (ball.c has the tables):
; texel (u, v) is in slot (b, s); the piece there is home tile h, turned or
; not; the texel is the picture's at the same place in tile h (16ths across
; and down, backwards if turned): 4 | c, or 4 (the groove), in both nibbles.
; Columns 0-31 again at 64-95.
.segment "DATA"
_tx_args:
tx_v0:    .byte 0
tx_v1:    .byte 0
tx_ident: .byte 0
tx_slots: .word 0
tx_s0:    .byte 0               ; the slots s0..s1-1 of each row
tx_s1:    .byte 0
tx_v:     .byte 0
tx_fv:    .byte 0
tx_gv:    .byte 0
tx_b:     .byte 0
tx_s:     .byte 0
tx_hs16:  .byte 0
tx_turn:  .byte 0
tx_end:   .byte 0
tx_bs:    .byte 0               ; b * S

.segment "CODE"
_tex_rows:
        lda     tx_v0
        sta     tx_v
@row:   lda     tx_v
        cmp     tx_v1
        bcc     :+
        rts
:       jsr     rowaddr         ; ptr2 = texl + v * 96
        lda     ptr2
        sta     @w1+1
        clc
        adc     #64
        sta     @w2+1
        lda     ptr2+1
        sta     @w1+2
        adc     #0
        sta     @w2+2
        ldx     tx_v
        lda     _fracv,x
        sta     tx_fv
        lda     _groove_v,x
        sta     tx_gv
        lda     _rowslot,x
        sta     tx_b
        tay                     ; b * S (< 128)
        lda     #0
        cpy     #0
        beq     @bs
:       clc
        adc     _cur_S
        dey
        bne     :-
@bs:    sta     tx_bs
        asl     a               ; ptr1 = slots + b * S * 2
        clc
        adc     tx_slots
        sta     ptr1
        lda     tx_slots+1
        adc     #0
        sta     ptr1+1
        lda     tx_s0
        sta     tx_s
@slot:  lda     tx_s
        cmp     tx_s1
        bcc     :+
        inc     tx_v
        jmp     @row
:       lda     tx_ident
        beq     @state
        lda     tx_bs           ; solved: home = b * S + s, upright
        clc
        adc     tx_s
        tax
        stz     tx_turn
        bra     @have
@state: lda     tx_s
        asl     a
        tay
        lda     (ptr1),y        ; home
        tax
        iny
        lda     (ptr1),y        ; orient
        sta     tx_turn
@have:  lda     _home_hs16,x
        sta     tx_hs16
        lda     tx_fv           ; the source row: srcv[hb16 + (turned ? 15 - fv : fv)]
        ldy     tx_turn
        beq     :+
        eor     #15
:       clc
        adc     _home_hb16,x
        tay
        lda     _srcv,y
        stz     ptr3+1          ; ptr3 = pic_buf + row * 16
        asl     a
        asl     a
        asl     a
        asl     a
        rol     ptr3+1
        clc
        adc     #<_pic_buf
        sta     ptr3
        lda     ptr3+1
        adc     #>_pic_buf
        sta     ptr3+1
        ldx     tx_s
        lda     _slot_u0+1,x
        sta     tx_end
        lda     _slot_u0,x
        tax                     ; X = u
@tex:   lda     #$44            ; the groove
        ldy     tx_gv
        bne     @put
        ldy     _groove_u,x
        bne     @put
        lda     _fracu,x
        ldy     tx_turn
        beq     :+
        eor     #15
:       clc
        adc     tx_hs16
        tay
        lda     _srcu,y         ; the picture's column (0..63)
        sta     tmp1
        lsr     a
        lsr     a
        tay
        lda     tmp1
        and     #3              ; 2 bits at 6 - 2k: shift right (3 - k) * 2
        eor     #3
        sta     tmp1
        lda     (ptr3),y
        ldy     tmp1
        beq     @got
:       lsr     a
        lsr     a
        dey
        bne     :-
@got:   and     #3
        tay
        lda     dbl,y
@put:
@w1:    sta     $FFFF,x
        cpx     #32
        bcs     :+
@w2:    sta     $FFFF,x         ; u + 64
:       inx
        cpx     tx_end
        bne     @tex
        inc     tx_s
        jmp     @slot
