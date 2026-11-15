; Lunar Rider (Lynx) - writing the sprite chain's SCBs each frame, in 6502.
;
; In cc65's C these took ~15,000 cycles a frame (the objects) and ~12,000
; (the terrain's features): a third of a 50 Hz frame. Here they are a few
; thousand together.
;
; objects_update: the bombs, enemies, bullets, the buggy and the explosions
; (positions in the original's 320 x 256 world -> Lynx x / 2, ytab[y]).
; features_update: the craters, rocks, hills and mines in view, from the
; feature list game.c keeps (feature i always uses SCB i & 31, so an SCB is
; set up once per feature, then only moved).

        .setcpu "65SC02"
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

        .import _bm_x, _bm_y, _bm_active
        .import _en_x, _en_y, _en_type, _en_active, _en_flash
        .import _fb_x, _fb_y, _fb_active, _ub_x, _ub_y, _ub_active
        .import _ex_x, _ex_y, _ex_radius, _ex_life, _ex_active
        .import _buggy_y, _buggy_alive, _buggy_wheel
        .import _scroll_px, _terrain
        .import _feat_wx_lo, _feat_wx_hi, _feat_ring_lo, _feat_ring_hi
        .import _feat_type, _feat_w, _feat_head, _feat_tail
        .import _s_bomb, _s_enemy, _s_bullet, _s_buggy, _s_ex, _s_feat, _feat_tmpl
        .import _ytab
        .import _meteor_data, _ufo_sm_frames, _ufo_lg_frames, _buggy_frames, _hill_frames
        .export _objects_update, _features_update, _features_reset, _feat_vis

SCB     = 23                    ; sizeof(SCB_REHV_PAL)
CTL1    = 1
DATA    = 5
HPOS    = 7
VPOS    = 9
HSIZE   = 11
VSIZE   = 13
PEN0    = 15
SKIP    = $04
YOFF    = 32                    ; ytab[y + 32]

N_FEAT  = 32
T_CRATER_LG = 2
T_ROCK  = 3
T_HILL  = 4
T_MINE  = 5

COL_WHITE    = 1
COL_RED      = 8
COL_ORANGE   = 9
COL_BRYELLOW = 13

        .bss
idx:    .res 1
idx2:   .res 1
vl:     .res 1                  ; a 16-bit value being worked on
vh:     .res 1
exl:    .res 1                  ; explosion centre
exh:    .res 1
eyl:    .res 1
eyh:    .res 1
ytop:   .res 1
_feat_vis: .res 1               ; the first feature not yet scrolled off to the left
fn:     .res 1
fi:     .res 1
fowner: .res N_FEAT             ; the feature each SCB shows

        .rodata
scb_lo: .repeat N_FEAT, I
        .byte <(_s_feat + I * SCB)
        .endrepeat
scb_hi: .repeat N_FEAT, I
        .byte >(_s_feat + I * SCB)
        .endrepeat
; feature type -> template (feat_tmpl[]: crater, rock, hill, mine)
tmpl_of: .byte 0, 0, 0, 1, 2, 3

        .code

; ------------------------------------------------------------ helpers

next_scb:
        clc
        lda     ptr1
        adc     #SCB
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       rts

hide:   ldy     #CTL1
        lda     (ptr1),y
        ora     #SKIP
        sta     (ptr1),y
        rts

show:   ldy     #CTL1
        lda     (ptr1),y
        and     #<~SKIP
        sta     (ptr1),y
        rts

; hpos = (vh:vl) >> 1 (arithmetic)
st_hpos_half:
        lda     vh
        cmp     #$80
        ror     a
        tax
        lda     vl
        ror     a
        ldy     #HPOS
        sta     (ptr1),y
        iny
        txa
        sta     (ptr1),y
        rts

; A = ytab[(vh:vl) + YOFF] (int8, as a line number)
ylook:  clc
        lda     vl
        adc     #<(_ytab + YOFF)
        sta     ptr2
        lda     vh
        adc     #>(_ytab + YOFF)
        sta     ptr2+1
        lda     (ptr2)
        rts

; vpos = ytab[(vh:vl) + YOFF], sign-extended
st_vpos:
        jsr     ylook
st_vpos_a:
        ldy     #VPOS
        sta     (ptr1),y
        iny
        cmp     #$80
        lda     #0
        bcc     :+
        lda     #$FF
:       sta     (ptr1),y
        rts

; vl/vh = 16-bit array word at offset X (idx2) of the array whose address is
; in the macro argument, minus a constant
.macro LOADW arr, sub
        lda     arr,x
        sec
        sbc     #<(sub)
        sta     vl
        lda     arr+1,x
        sbc     #>(sub)
        sta     vh
.endmacro

; ------------------------------------------------------------ objects

.proc _objects_update
        ; ---- bombs: (x - 1) / 2, Y(y - 1)
        lda     #<_s_bomb
        sta     ptr1
        lda     #>_s_bomb
        sta     ptr1+1
        stz     idx
bomb:   ldx     idx
        lda     _bm_active,x
        bne     :+
        jsr     hide
        bra     bomb_nx
:       jsr     show
        lda     idx
        asl     a
        tax
        LOADW   _bm_x, 1
        jsr     st_hpos_half
        ldx     idx
        txa
        asl     a
        tax
        LOADW   _bm_y, 1
        jsr     st_vpos
bomb_nx:
        jsr     next_scb
        inc     idx
        lda     idx
        cmp     #8
        bne     bomb

        ; ---- enemies: x / 2; UFOs Y(y) with their flash frame, meteors Y(y - 2)
        lda     #<_s_enemy
        sta     ptr1
        lda     #>_s_enemy
        sta     ptr1+1
        stz     idx
enemy:  ldx     idx
        lda     _en_active,x
        bne     :+
        jsr     hide
        jmp     enemy_nx
:       jsr     show
        txa
        asl     a
        sta     idx2
        tax
        LOADW   _en_x, 0
        jsr     st_hpos_half
        ldx     idx
        lda     _en_type,x
        cmp     #3
        bne     ufo
        ; meteor
        ldy     #DATA
        lda     #<_meteor_data
        sta     (ptr1),y
        iny
        lda     #>_meteor_data
        sta     (ptr1),y
        ldx     idx2
        LOADW   _en_y, 2
        jsr     st_vpos
        bra     enemy_nx
ufo:    sta     tmp1                    ; 1 small, 2 large
        lda     _en_flash,x
        lsr     a
        lsr     a
        and     #2                      ; ((flash >> 3) & 1) * 2
        tax
        ldy     #DATA
        lda     tmp1
        cmp     #1
        bne     large
        lda     _ufo_sm_frames,x
        sta     (ptr1),y
        iny
        lda     _ufo_sm_frames+1,x
        sta     (ptr1),y
        bra     ufo_y
large:  lda     _ufo_lg_frames,x
        sta     (ptr1),y
        iny
        lda     _ufo_lg_frames+1,x
        sta     (ptr1),y
ufo_y:  ldx     idx2
        LOADW   _en_y, 0
        jsr     st_vpos
enemy_nx:
        jsr     next_scb
        inc     idx
        lda     idx
        cmp     #8
        beq     bullets
        jmp     enemy

        ; ---- bullets: forward then upward, x / 2, Y(y)
bullets:
        lda     #<_s_bullet
        sta     ptr1
        lda     #>_s_bullet
        sta     ptr1+1
        stz     idx
fwd:    ldx     idx
        lda     _fb_active,x
        bne     :+
        jsr     hide
        bra     fwd_nx
:       jsr     show
        txa
        asl     a
        tax
        LOADW   _fb_x, 0
        jsr     st_hpos_half
        lda     idx
        asl     a
        tax
        LOADW   _fb_y, 0
        jsr     st_vpos
fwd_nx: jsr     next_scb
        inc     idx
        lda     idx
        cmp     #2
        bne     fwd
        stz     idx
up:     ldx     idx
        lda     _ub_active,x
        bne     :+
        jsr     hide
        bra     up_nx
:       jsr     show
        txa
        asl     a
        tax
        LOADW   _ub_x, 0
        jsr     st_hpos_half
        lda     idx
        asl     a
        tax
        LOADW   _ub_y, 0
        jsr     st_vpos
up_nx:  jsr     next_scb
        inc     idx
        lda     idx
        cmp     #2
        bne     up

        ; ---- the buggy: Y(y - 5), its wheel frame
        lda     #<_s_buggy
        sta     ptr1
        lda     #>_s_buggy
        sta     ptr1+1
        lda     _buggy_alive
        bne     :+
        jsr     hide
        bra     explosions
:       jsr     show
        lda     _buggy_y
        sec
        sbc     #5
        sta     vl
        lda     _buggy_y+1
        sbc     #0
        sta     vh
        jsr     st_vpos
        lda     _buggy_wheel
        asl     a
        tax
        ldy     #DATA
        lda     _buggy_frames,x
        sta     (ptr1),y
        iny
        lda     _buggy_frames+1,x
        sta     (ptr1),y

        ; ---- explosions: an outer square (white, then orange) and from
        ; radius 4 an inner one of half the radius (bright yellow, then red)
explosions:
        lda     #<_s_ex
        sta     ptr1
        lda     #>_s_ex
        sta     ptr1+1
        stz     idx
ex:     ldx     idx
        lda     _ex_active,x
        bne     :+
        jsr     hide
        jsr     next_scb
        jsr     hide
        bra     ex_nx
:       txa
        asl     a
        tax
        lda     _ex_x,x
        sta     exl
        lda     _ex_x+1,x
        sta     exh
        lda     _ex_y,x
        sta     eyl
        lda     _ex_y+1,x
        sta     eyh
        ldx     idx
        lda     _ex_radius,x
        sta     tmp3
        lda     #COL_WHITE
        ldy     _ex_life,x
        cpy     #7
        bcs     :+
        lda     #COL_ORANGE
:       jsr     exbox
        jsr     next_scb
        lda     tmp3
        cmp     #4
        bcs     :+
        jsr     hide
        bra     ex_nx
:       lsr     a
        sta     tmp3
        ldx     idx
        lda     #COL_BRYELLOW
        ldy     _ex_life,x
        cpy     #9
        bcs     :+
        lda     #COL_RED
:       jsr     exbox
ex_nx:  jsr     next_scb
        inc     idx
        lda     idx
        cmp     #6
        bne     ex
        rts
.endproc

; the square of radius tmp3 around (exh:exl, eyh:eyl) in colour A
.proc exbox
        ldy     #PEN0
        sta     (ptr1),y
        jsr     show
        ; hpos = (x - r) / 2
        lda     exl
        sec
        sbc     tmp3
        sta     vl
        lda     exh
        sbc     #0
        sta     vh
        jsr     st_hpos_half
        ; hsize = r + 1
        ldy     #HSIZE
        lda     #0
        sta     (ptr1),y
        iny
        lda     tmp3
        inc     a
        sta     (ptr1),y
        ; vpos = Y(y - r), vsize = Y(y + r) - Y(y - r) + 1
        lda     eyl
        sec
        sbc     tmp3
        sta     vl
        lda     eyh
        sbc     #0
        sta     vh
        jsr     ylook
        sta     ytop
        jsr     st_vpos_a
        lda     eyl
        clc
        adc     tmp3
        sta     vl
        lda     eyh
        adc     #0
        sta     vh
        jsr     ylook
        sec
        sbc     ytop
        inc     a
        ldy     #VSIZE+1
        sta     (ptr1),y
        dey
        lda     #0
        sta     (ptr1),y
        rts
.endproc

; ------------------------------------------------------------ features

; void features_reset(void): every feature SCB hidden and owned by nobody
.proc _features_reset
        ldx     #N_FEAT - 1
:       lda     scb_lo,x
        sta     ptr1
        lda     scb_hi,x
        sta     ptr1+1
        jsr     hide
        txa
        eor     #1                      ; a feature index that never maps to SCB x
        sta     fowner,x
        dex
        bpl     :-
        stz     _feat_vis
        rts
.endproc

; vh:vl = feat_wx[X] - scroll_px
.macro REL
        lda     _feat_wx_lo,x
        sec
        sbc     _scroll_px
        sta     vl
        lda     _feat_wx_hi,x
        sbc     _scroll_px+1
        sta     vh
.endmacro

.proc _features_update
        ; feat_vis back to the head if the list moved past it (new game)
        lda     _feat_vis
        sec
        sbc     _feat_head
        sta     tmp1
        lda     _feat_tail
        sec
        sbc     _feat_head
        cmp     tmp1
        bcs     adv
        lda     _feat_head
        sta     _feat_vis

        ; skip (and hide) the features that scrolled off to the left:
        ; rel + w <= 0
adv:    ldx     _feat_vis
        cpx     _feat_tail
        beq     adv_done
        REL
        lda     vl
        clc
        adc     _feat_w,x
        tay
        lda     vh
        adc     #0
        bmi     passed
        bne     adv_done
        tya
        bne     adv_done
passed: txa
        and     #N_FEAT - 1
        tay
        txa
        cmp     fowner,y
        bne     :+
        lda     scb_lo,y
        sta     ptr1
        lda     scb_hi,y
        sta     ptr1+1
        jsr     hide
:       inc     _feat_vis
        bra     adv
adv_done:

        ; the features in view, up to 32
        lda     _feat_vis
        sta     fi
        stz     fn
loop:   ldx     fi
        cpx     _feat_tail
        bne     :+
        rts
:       lda     fn
        cmp     #N_FEAT
        bne     :+
        rts
:       REL
        lda     vh                      ; rel >= 320: beyond the right edge, done
        bmi     in_view
        beq     in_view
        cmp     #1
        beq     :+
        rts
:       lda     vl
        cmp     #<320
        bcc     in_view
        rts
in_view:
        txa
        and     #N_FEAT - 1
        tay
        lda     scb_lo,y
        sta     ptr1
        lda     scb_hi,y
        sta     ptr1+1
        txa
        cmp     fowner,y
        beq     :+
        sta     fowner,y
        jsr     setup
        ldx     fi
:       lda     _feat_type,x
        cmp     #T_ROCK
        beq     check
        cmp     #T_MINE
        bne     visible
check:  sta     tmp3                    ; still there? (shot, or blown up)
        clc
        lda     _feat_ring_lo,x
        adc     #<_terrain
        sta     ptr2
        lda     _feat_ring_hi,x
        adc     #>_terrain
        sta     ptr2+1
        lda     (ptr2)
        cmp     tmp3
        beq     visible
        jsr     hide
        bra     next
visible:
        jsr     show
        jsr     st_hpos_half
next:   inc     fn
        inc     fi
        jmp     loop
.endproc

; set SCB ptr1 up for feature X from its template (all but `next`), then its
; width: a crater's box is (w + 1) / 2 wide, a hill cut short shows its first
; (w + 1) / 2 columns
.proc setup
        ldy     _feat_type,x
        lda     tmpl_of,y
        tay
        lda     #<_feat_tmpl
        sta     ptr2
        lda     #>_feat_tmpl
        sta     ptr2+1
:       dey
        bmi     :+
        clc
        lda     ptr2
        adc     #SCB
        sta     ptr2
        bcc     :-
        inc     ptr2+1
        bra     :-
:       ldy     #2
copy1:  lda     (ptr2),y
        sta     (ptr1),y
        dey
        bpl     copy1
        ldy     #5
copy2:  lda     (ptr2),y
        sta     (ptr1),y
        iny
        cpy     #SCB
        bne     copy2
        lda     _feat_type,x
        cmp     #T_HILL
        beq     hill
        cmp     #T_CRATER_LG + 1
        bcs     done
        lda     _feat_w,x               ; crater
        inc     a
        lsr     a
        ldy     #HSIZE+1
        sta     (ptr1),y
done:   rts
hill:   lda     _feat_w,x               ; hill_frames[(w - 1) / 2]
        dec     a
        and     #$FE                    ; ((w - 1) >> 1) * 2
        tay
        lda     _hill_frames,y
        pha
        lda     _hill_frames+1,y
        ldy     #DATA+1
        sta     (ptr1),y
        dey
        pla
        sta     (ptr1),y
        rts
.endproc

; ------------------------------------------------------------ sky erase

; void erase_save(void): the black boxes that will erase, the next time this
; screen buffer is drawn, what was drawn in the sky this frame: one box per
; bomb, enemy, upward bullet and explosion (an enemy's box is the large
; UFO's size, any bigger is harmless: stars, HUD and mountains are drawn
; after the boxes). _ers_set points at this buffer's 24 boxes.
        .import _ers_set
        .export _erase_save

N_ERS = 24
        .rodata
ers_src_lo:
        .repeat 8, I
        .byte <(_s_bomb + I * SCB)
        .endrepeat
        .repeat 8, I
        .byte <(_s_enemy + I * SCB)
        .endrepeat
        .repeat 2, I
        .byte <(_s_bullet + (2 + I) * SCB)
        .endrepeat
        .repeat 6, I
        .byte <(_s_ex + I * 2 * SCB)
        .endrepeat
ers_src_hi:
        .repeat 8, I
        .byte >(_s_bomb + I * SCB)
        .endrepeat
        .repeat 8, I
        .byte >(_s_enemy + I * SCB)
        .endrepeat
        .repeat 2, I
        .byte >(_s_bullet + (2 + I) * SCB)
        .endrepeat
        .repeat 6, I
        .byte >(_s_ex + I * 2 * SCB)
        .endrepeat
; width, height (0 = the source SCB's own size)
ers_w:  .byte 2, 2, 2, 2, 2, 2, 2, 2
        .byte 10, 10, 10, 10, 10, 10, 10, 10
        .byte 1, 1
        .byte 0, 0, 0, 0, 0, 0
ers_h:  .byte 2, 2, 2, 2, 2, 2, 2, 2
        .byte 5, 5, 5, 5, 5, 5, 5, 5
        .byte 2, 2
        .byte 0, 0, 0, 0, 0, 0

        .code
.proc _erase_save
        lda     _ers_set
        sta     ptr1
        lda     _ers_set+1
        sta     ptr1+1
        ldx     #0
loop:   lda     ers_src_lo,x
        sta     ptr2
        lda     ers_src_hi,x
        sta     ptr2+1
        ldy     #CTL1
        lda     (ptr2),y
        and     #SKIP
        beq     :+
        jsr     hide
        bra     next
:       jsr     show
        ldy     #HPOS
:       lda     (ptr2),y                ; hpos, vpos
        sta     (ptr1),y
        iny
        cpy     #VPOS + 2
        bne     :-
        lda     ers_w,x
        beq     same
        ldy     #HSIZE+1
        sta     (ptr1),y
        lda     ers_h,x
        ldy     #VSIZE+1
        sta     (ptr1),y
        bra     next
same:   ldy     #HSIZE+1
        lda     (ptr2),y
        sta     (ptr1),y
        ldy     #VSIZE+1
        lda     (ptr2),y
        sta     (ptr1),y
next:   jsr     next_scb
        inx
        cpx     #N_ERS
        bne     loop
        rts
.endproc
