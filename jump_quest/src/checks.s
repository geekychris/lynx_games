; jq.c's per-tick box tests and particle update in asm (in C the 24 items
; alone were ~9k cycles a tick); the C in jq.c is the host's, and the
; reference. Each overlap test is the original's pair of compares as one
; unsigned compare an axis, as the Spectrum port's checks.asm:
;     (uint16)(b.pos + k) < l
; Item: type 0, x 1, y 3, active 5 (9 bytes). Enemy: x 1, y 3, alive 10 (13).
; Particle: x 0, y 2, vx 4, vy 6, life 8 (10).
        .export _items_check_collect, _enemies_check, _particles_update
        .import _game, incsp6
        .importzp c_sp

ENEMIES   = 56
ITEMS     = 56 + 16 * 13
PARTICLES = ITEMS + 24 * 9

        .zeropage
kx:     .res 2
lx:     .res 2
ky:     .res 2
ly:     .res 2
cp:     .res 2
cn:     .res 1
cstep:  .res 1
calive: .res 1
ph:     .res 2
stomp:  .res 1

        .code
; kx = A - px, ky = A - py (px at sp+4, py at sp+2 after ph was taken)
.proc set_k
        pha
        ldy #4
        sec
        sbc (c_sp),y
        sta kx
        iny
        lda #0
        sbc (c_sp),y
        sta kx+1
        pla
        ldy #2
        sec
        sbc (c_sp),y
        sta ky
        iny
        lda #0
        sbc (c_sp),y
        sta ky+1
        rts
.endproc

; lx = pw + A (pw at sp+0)
.proc set_lx
        ldy #0
        clc
        adc (c_sp),y
        sta lx
        iny
        lda (c_sp),y
        adc #0
        sta lx+1
        rts
.endproc

; ly = ph + A
.proc set_ly
        clc
        adc ph
        sta ly
        lda ph+1
        adc #0
        sta ly+1
        rts
.endproc

; the first entry from cp (cn of them, cstep bytes, live flag at calive)
; in both ranges: C set and cp at it, else C clear
.proc scan
@loop:  ldy calive
        lda (cp),y
        beq @next
        ldy #1                  ; x + kx < lx
        lda (cp),y
        clc
        adc kx
        tax
        iny
        lda (cp),y
        adc kx+1
        cpx lx
        sbc lx+1
        bcs @next
        iny                     ; y + ky < ly
        lda (cp),y
        clc
        adc ky
        tax
        iny
        lda (cp),y
        adc ky+1
        cpx ly
        sbc ly+1
        bcc @hit
@next:  lda cp
        clc
        adc cstep
        sta cp
        bcc :+
        inc cp+1
:       dec cn
        bne @loop
        clc
        rts
@hit:   sec
        rts
.endproc

.proc items_setup
        lda #<(_game + ITEMS)
        sta cp
        lda #>(_game + ITEMS)
        sta cp+1
        lda #24
        sta cn
        lda #9
        sta cstep
        lda #5
        sta calive
        rts
.endproc

.proc enemies_setup
        lda #<(_game + ENEMIES)
        sta cp
        lda #>(_game + ENEMIES)
        sta cp+1
        lda #16
        sta cn
        lda #13
        sta cstep
        lda #10
        sta calive
        rts
.endproc

; int8_t items_check_collect(px, py, pw, ph): -14 < x - px < pw - 2 and
; -14 < y - py < ph - 2; the first such item is collected, its type returned
_items_check_collect:
        sta ph
        stx ph+1
        lda #13
        jsr set_k
        lda #11
        jsr set_lx
        lda #11
        jsr set_ly
        jsr items_setup
        jsr scan
        bcc ret0
        ldy #5
        lda #0
        sta (cp),y
        tay
        lda (cp),y              ; the type
        ldx #0
        jmp incsp6

ret0:   lda #0
        tax
        jmp incsp6

; uint8_t enemies_check(px, py, pw, ph, stomp): 2 if stomp and one is stomped
; (x as an item; feet - 10 < y < feet), else 1 if one is touched
; (-13 < x - px < pw - 3, -13 < y - py < ph - 3)
_enemies_check:
        sta stomp
        ldy #0
        lda (c_sp),y
        sta ph
        iny
        lda (c_sp),y
        sta ph+1
        jsr incsp2_             ; (pw, py, px at sp+0, 2, 4 as for set_k)
        lda stomp
        beq @touch
        lda #13
        jsr set_k
        lda ky                  ; ky = 9 - py - ph
        sec
        sbc #4
        sta ky
        lda ky+1
        sbc #0
        sta ky+1
        lda ky
        sec
        sbc ph
        sta ky
        lda ky+1
        sbc ph+1
        sta ky+1
        lda #11
        jsr set_lx
        lda #9
        sta ly
        lda #0
        sta ly+1
        jsr enemies_setup
        jsr scan
        bcc @touch
        ldy #10
        lda #0
        sta (cp),y
        lda #2
        bne @ret
@touch: lda #12
        jsr set_k
        lda #9
        jsr set_lx
        lda #9
        jsr set_ly
        jsr enemies_setup
        jsr scan
        lda #0
        rol a
@ret:   ldx #0
        pha
        lda c_sp                  ; pop pw, py, px
        clc
        adc #6
        sta c_sp
        bcc :+
        inc c_sp+1
:       pla
        rts

incsp2_:
        lda c_sp
        clc
        adc #2
        sta c_sp
        bcc :+
        inc c_sp+1
:       rts

; particles_update(): each live one moves, falls and ages
_particles_update:
        lda #<(_game + PARTICLES)
        sta cp
        lda #>(_game + PARTICLES)
        sta cp+1
        lda #16
        sta cn
@loop:  ldy #8
        lda (cp),y
        beq @next
        bmi @next
        sec
        sbc #1
        sta (cp),y
        ldy #0                  ; x += vx
        jsr addv
        ldy #2                  ; y += vy
        jsr addv
        ldy #6                  ; vy += 1
        lda (cp),y
        clc
        adc #1
        sta (cp),y
        iny
        lda (cp),y
        adc #0
        sta (cp),y
@next:  lda cp
        clc
        adc #10
        sta cp
        bcc :+
        inc cp+1
:       dec cn
        bne @loop
        rts

; the word at Y += the word at Y + 4
addv:   iny
        iny
        iny
        iny
        lda (cp),y
        tax
        iny
        lda (cp),y
        dey
        dey
        dey
        dey
        pha
        dey
        txa
        clc
        adc (cp),y
        sta (cp),y
        iny
        pla
        adc (cp),y
        sta (cp),y
        rts
