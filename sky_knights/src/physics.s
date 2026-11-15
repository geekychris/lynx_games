; Sky Knights: one body's physics in 6502 (game.c's body_step in C was
; 3-6K cycles a body; this is ~300-500). From the Atari 8-bit port
; (a8dev/projects/sky_knights/src/physics.s); the same 65(SC)02 code.
;
; body_step(b): the original's code for players and enemies (the same):
; gravity, move, wrap, the ceiling, the ledge undersides (players: bs_above),
; landing, the floor. Returns on_ground. The ledges come from game.c's
; tables: plat_rows[py] has bit i set when ledge i's y range (y..y+8)
; holds line py, so a body away from every ledge costs one lookup; the x
; tests (px > x - 20, px < x + w) and the y a body is put at are tables too.
; Fixed point 1/64 (game.h); positions stay in 0..16383 vertically.

        .export _body_step, _bs_above
        .import _b_px, _b_py
        .import _b_x, _b_y, _b_dy, _b_dx, _b_on_ground, _gravity
        .import _plat_rows, _plat_xa_lo, _plat_xa_hi, _plat_xb_lo, _plat_xb_hi
        .import _plat_ut_lo, _plat_ut_hi, _plat_tp_lo, _plat_tp_hi

        .segment "ZEROPAGE"
yy:     .res 2
dy:     .res 2
px:     .res 2
t:      .res 1
on:     .res 1
pxok:   .res 1
mask:   .res 1

        .bss
_bs_above: .res 1

; signed 16-bit compare of (lo, hi) against K: N set when less
.macro  scmp lo, hi, K
        lda lo
        cmp #<(K)
        lda hi
        sbc #>(K)
        bvc :+
        eor #$80
:
.endmacro

        .code
_body_step:
        asl a
        tax                             ; X = b * 2 (the int arrays)
        ; gravity, at most MAX_FALL_VEL (256)
        clc
        lda _b_dy,x
        adc _gravity
        sta dy
        lda _b_dy+1,x
        adc _gravity+1
        sta dy+1
        bmi @fallok
        cmp #1
        bcc @fallok
        bne @clampf
        lda dy
        beq @fallok
@clampf: lda #0
        sta dy
        lda #1
        sta dy+1
@fallok:
        ; move
        clc
        lda _b_x,x
        adc _b_dx,x
        sta _b_x,x
        lda _b_x+1,x
        adc _b_dx+1,x
        sta _b_x+1,x
        clc
        lda _b_y,x
        adc dy
        sta yy
        lda _b_y+1,x
        adc dy+1
        sta yy+1
        ; the wrap: px < -20 -> 320, px > 320 -> -19
        scmp {_b_x,x}, {_b_x+1,x}, -20 * 64
        bpl @notl
        lda #<(320 * 64)
        sta _b_x,x
        lda #>(320 * 64)
        sta _b_x+1,x
        jmp @wrapped
@notl:  scmp {_b_x,x}, {_b_x+1,x}, 321 * 64
        bmi @wrapped
        lda #<(-19 * 64)
        sta _b_x,x
        lda #>(-19 * 64)
        sta _b_x+1,x
@wrapped:
        ; the ceiling: y < 10
        scmp yy, yy+1, 10 * 64
        bpl @noceil
        lda #<(10 * 64)
        sta yy
        lda #>(10 * 64)
        sta yy+1
        lda #0
        sta dy
        sta dy+1
@noceil:
        lda #0
        sta pxok
        ; rising into a ledge from below (players): ledges 1.. at py
        lda _bs_above
        beq @land
        lda dy+1
        bpl @land
        jsr pyint
        tay
        lda _plat_rows,y
        and #$FE
        beq @land
        jsr findx
        bcc @land
        lda _plat_ut_lo,y               ; y = the ledge's underside
        sta yy
        lda _plat_ut_hi,y
        sta yy+1
        lda #0
        sta dy
        sta dy+1
@land:  ; landing on a ledge (falling or still): at py + 20 (the feet)
        lda #0
        sta on
        jsr pyint
        clc
        adc #20
        bcs @floor                      ; below line 255: no ledge
        tay
        lda _plat_rows,y
        beq @floor
        jsr findx
        bcc @floor
        lda dy+1
        bmi @floor
        lda _plat_tp_lo,y               ; y = on top of it
        sta yy
        lda _plat_tp_hi,y
        sta yy+1
        lda #0
        sta dy
        sta dy+1
        inc on
@floor: scmp yy, yy+1, (256 - 20) * 64
        bmi @store
        lda #<((256 - 20) * 64)
        sta yy
        lda #>((256 - 20) * 64)
        sta yy+1
        lda #0
        sta dy
        sta dy+1
        lda #1
        sta on
@store: jsr pxget                       ; the whole-pixel position
        lda px
        sta _b_px,x
        lda px+1
        sta _b_px+1,x
        lda yy
        sta _b_y,x
        lda yy+1
        sta _b_y+1,x
        lda dy
        sta _b_dy,x
        lda dy+1
        sta _b_dy+1,x
        txa
        lsr a
        tay
        jsr pyint
        sta _b_py,y
        lda on
        sta _b_on_ground,y
        ldx #0
        rts

; A = yy >> 6 (yy is 0..16383)
pyint:  lda yy
        sta t
        lda yy+1
        asl t
        rol a
        asl t
        rol a
        rts

; px = b_x >> 6 (signed), once a step
pxget:  lda pxok
        bne @done
        inc pxok
        lda _b_x,x                      ; px = b_x >> 6, signed
        sta t
        lda #0
        sta px+1
        lda _b_x+1,x
        bpl :+
        dec px+1
:       asl t
        rol a
        rol px+1
        asl t
        rol a
        rol px+1
        sta px
@done:  rts

; the first ledge in mask (bit i = ledge i) whose x range holds the body:
; carry set and Y = the ledge, or carry clear. Works out px once.
findx:  sta mask
        jsr pxget
@have:  ldy #0
@next:  lsr mask
        bcc @skip
        ; px >= x - 19 and px < x + w
        lda px
        cmp _plat_xa_lo,y
        lda px+1
        sbc _plat_xa_hi,y
        bvc :+
        eor #$80
:       bmi @skip
        lda px
        cmp _plat_xb_lo,y
        lda px+1
        sbc _plat_xb_hi,y
        bvc :+
        eor #$80
:       bmi @hit
@skip:  iny
        lda mask
        bne @next
        clc
        rts
@hit:   sec
        rts

; ---- the enemies' step (game.c's update_enemy for each, in order): the
; flap timer (enemy_flap in C when it runs out), the physics, every 64
; frames raiders and wraiths turn to the nearest knight (enemy_seek), a
; swooper turns at random (1 in 128 a frame), the wing animation.

        .export _update_enemies, _check_combat, _jo_i, _jo_e
        .import _e_active, _e_type, _e_flap_timer, _b_facing, _b_anim_timer, _b_anim_frame
        .import _b_px, _b_py, _p_alive, _gs
        .import _enemy_flap, _enemy_seek, _joust
        .export _dice8, _dice8_seed

GS_FRAME   = 14                         ; GameState: score[2] (8), wave, state,
GS_PLAYERS = 16                         ; state_timer, frame, num_players
NEN        = 8
ENEMY_SLOW = 64

        .segment "ZEROPAGE"
ui:     .res 1
ci:     .res 1
cpx_:   .res 2
cpy_:   .res 1

        .bss
_jo_i:  .res 1
_jo_e:  .res 1

        .code
_update_enemies:
        lda #0
        sta ui
@loop:  ldx ui
        lda _e_active,x
        beq @next
        dec _e_flap_timer,x
        bne :+
        txa
        jsr _enemy_flap
:       lda #0
        sta _bs_above
        lda ui
        clc
        adc #2
        jsr _body_step
        ldx ui
        lda _gs+GS_FRAME
        and #63
        bne @noseek
        lda _e_type,x
        beq @noseek
        txa
        jsr _enemy_seek
        ldx ui
@noseek:
        lda _e_type,x
        bne @anim
        jsr _dice8
        and #127
        bne @anim
        ldx ui                          ; turn: facing ^= 1, dx = -/+ slow
        lda _b_facing+2,x
        eor #1
        sta _b_facing+2,x
        tay
        txa
        asl a
        tax                             ; b_dx of body i + 2: X + 4
        cpy #0
        bne @left
        lda #<ENEMY_SLOW
        sta _b_dx+4,x
        lda #>ENEMY_SLOW
        sta _b_dx+5,x
        jmp @anim
@left:  lda #<(-ENEMY_SLOW)
        sta _b_dx+4,x
        lda #>(-ENEMY_SLOW)
        sta _b_dx+5,x
@anim:  ldx ui
        inc _b_anim_timer+2,x
        lda _b_anim_timer+2,x
        cmp #9
        bcc @next
        lda #0
        sta _b_anim_timer+2,x
        lda _b_anim_frame+2,x
        eor #1
        sta _b_anim_frame+2,x
@next:  inc ui
        lda ui
        cmp #NEN
        beq :+
        jmp @loop
:       rts

; ---- the jousts (game.c's check_combat): for each living knight, each
; enemy whose box overlaps its box (|dy| < 20, |dx| < 20, y first, in a
; byte) - joust() (C, jo_i and jo_e) decides it; it returns 1 when the
; knight went down (no more for it this step)

_check_combat:
        lda #0
        sta ci
@player:
        ldx ci
        cpx _gs+GS_PLAYERS
        bcs @done
        lda _p_alive,x
        beq @nextp
        lda _b_py,x
        sta cpy_
        txa
        asl a
        tax
        lda _b_px,x
        sta cpx_
        lda _b_px+1,x
        sta cpx_+1
        lda #0
        sta ui
@enemy: ldx ui
        lda _e_active,x
        beq @nexte
        lda cpy_                        ; py - ey + 19 < 39
        sec
        sbc _b_py+2,x
        clc
        adc #19
        cmp #39
        bcs @nexte
        txa
        asl a
        tay
        lda cpx_                        ; px - ex + 19 in 0..38
        sec
        sbc _b_px+4,y
        tax
        lda cpx_+1
        sbc _b_px+5,y
        tay
        txa
        clc
        adc #19
        bcc :+
        iny
:       cpy #0
        bne @nexte
        cmp #39
        bcs @nexte
        lda ci
        sta _jo_i
        lda ui
        sta _jo_e
        jsr _joust
        cmp #0
        bne @nextp
@nexte: inc ui
        lda ui
        cmp #NEN
        bne @enemy
@nextp: inc ci
        jmp @player
@done:  rts

; ---- dice8: an 8-bit LFSR (period 255; ~20 cycles, the SDK's rnd16 is
; several hundred): the per-frame dice (a swooper's turn,
; the flap delays). game_start seeds it (never 0).
        .data
_dice8_seed: .byte 1
        .code
_dice8:  lda _dice8_seed
        asl a
        bcc :+
        eor #$1D
:       sta _dice8_seed
        ldx #0
        rts
