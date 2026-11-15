; enemies_update(): the original's enemy_update() for each live enemy (the C
; is in jq.c for the host): 39k cycles a tick in C, the most of a tick.
; Enemy: type 0, x 1, y 3, vx 5, vy 7, dir 9, alive 10, frame 11, timer 12.
        .export _enemies_update, _enemies_rows
        .importzp tp_x
        .import _game, _jq_solid, _jq_fly_dy, _jq_level_w
        .import tile_xy, _tmap_row, _tmap_w, _tmap_h

ENEMIES  = 56                   ; offsetof(Game, enemies) (checked in jq.c)
ESIZE    = 13
NENEMIES = 16
E_TYPE = 0
E_X    = 1
E_Y    = 3
E_VX   = 5
E_VY   = 7
E_DIR  = 9
E_ALIVE = 10
E_FRAME = 11
E_TIMER = 12
TILE_PLATFORM = 10

        .zeropage
ep:     .res 2
ecount: .res 1
spd:    .res 1
ex:     .res 2
ey:     .res 2
qy:     .res 2
eidx:   .res 1
rp:     .res 2
cl:     .res 1                  ; the left edge's column ($FF: off the map)
cr:     .res 1                  ; the right edge's

        .bss
; a walker's rows (it never moves up or down): y, y + 12 and y + 16 as
; tmap row pointers (hi 0: off the map)
rm_lo:  .res 16
rm_hi:  .res 16
rf_lo:  .res 16
rf_hi:  .res 16
rb_lo:  .res 16
rb_hi:  .res 16
; the walls and floors it tests depend only on its columns and direction:
; the key of the last test (x & $FFF0, x & 15 >= 2 (x + 14 in the next
; column), dir < 0) and the direction it gave. Tiles that change (? blocks)
; stay solid.
ck_lo:  .res 16
ck_hi:  .res 16
ck_dir: .res 16
flyw:   .res 2                  ; jq_level_w - 16: a fly turns at x >= this

        .code
; the tile at (tp_x, ey + A (0..16)) -> A
.proc tile_at_dy
        clc
        adc ey
        pha
        lda ey+1
        adc #0
        tax
        pla
        jmp tile_xy
.endproc

; tp_x = ex + A
.proc set_tx
        clc
        adc ex
        sta tp_x
        lda ex+1
        adc #0
        sta tp_x+1
        rts
.endproc

; spd = FP_INT(the word at ep, Y) (vx or vy: 0..FP(3))
.proc get_speed
        iny
        lda (ep),y
        sta spd
        dey
        lda (ep),y
        asl a
        rol spd
        asl a
        rol spd
        rts
.endproc

_enemies_update:
        lda #<(_game + ENEMIES)
        sta ep
        lda #>(_game + ENEMIES)
        sta ep+1
        lda #NENEMIES
        sta ecount
        lda #0
        sta eidx
@loop:  ldy #E_ALIVE
        lda (ep),y
        beq @next
        jsr one
@next:  lda ep
        clc
        adc #ESIZE
        sta ep
        bcc :+
        inc ep+1
:       inc eidx
        dec ecount
        bne @loop
        rts

.proc one
        ldy #E_TIMER            ; timer++, frame = (timer >> 3) & 1
        lda (ep),y
        clc
        adc #1
        sta (ep),y
        lsr a
        lsr a
        lsr a
        and #1
        ldy #E_FRAME
        sta (ep),y
        ldy #E_TYPE
        lda (ep),y
        cmp #2
        beq walker
        cmp #5
        beq walker
        cmp #3
        bne :+
        jmp fly
:       cmp #4
        bne :+
        jmp spider
:       rts

walker:
        ldy #E_VX
        jsr get_speed
        jsr move_x              ; ex = the new x
        ldx eidx
        lda ex                  ; the key
        and #15
        cmp #2                  ; C: x + 14 is in the next column
        lda #0
        rol a
        sta qy
        ldy #E_DIR
        lda (ep),y
        and #2                  ; (-1: $FF)
        ora qy
        sta qy
        lda ex
        and #$F0
        ora qy
        cmp ck_lo,x
        bne @miss
        lda ex+1
        cmp ck_hi,x
        bne @miss
        lda ck_dir,x            ; as last time
        sta (ep),y
        rts
@miss:  lda ex
        and #$F0
        ora qy
        ldy ex+1
        sta ck_lo,x
        tya
        sta ck_hi,x
        jsr @test
        ldx eidx
        ldy #E_DIR
        lda (ep),y
        sta ck_dir,x
        rts
@test:  lda #0
        jsr col_of
        sta cl
        lda #14
        jsr col_of
        sta cr
        ldx eidx
        ldy #E_DIR
        lda (ep),y
        bpl @wr
        ldy cl                  ; left: a wall at the left edge turns it
        jsr wall
        beq @edge
        lda #1
        bne @setd
@wr:    ldy cr                  ; right: at the right edge
        jsr wall
        beq @edge
        lda #$FF
@setd:  ldy #E_DIR
        sta (ep),y
@edge:  ldy #E_DIR              ; don't walk off platforms
        lda (ep),y
        bpl @er
        ldy cl
        jsr floor
        bne @er
        lda #1
        ldy #E_DIR
        sta (ep),y
@er:    ldy #E_DIR
        lda (ep),y
        bmi @done
        beq @done
        ldy cr
        jsr floor
        bne @done
        lda #$FF
        ldy #E_DIR
        sta (ep),y
@done:  rts

; Z clear if column Y (of walker X) has a solid tile at y or y + 12
wall:   cpy #$FF
        beq @no
        lda rm_hi,x
        beq @f
        sta rp+1
        lda rm_lo,x
        sta rp
        lda (rp),y
        sty qy
        tay
        lda _jq_solid,y
        bne @yes
        ldy qy
@f:     lda rf_hi,x
        beq @no
        sta rp+1
        lda rf_lo,x
        sta rp
        lda (rp),y
        tay
        lda _jq_solid,y
@yes:   rts
@no:    lda #0
        rts

; Z clear if column Y has ground (solid or a platform) at y + 16
floor:  cpy #$FF
        beq @no
        lda rb_hi,x
        beq @no
        sta rp+1
        lda rb_lo,x
        sta rp
        lda (rp),y
        cmp #TILE_PLATFORM
        beq @yes
        tay
        lda _jq_solid,y
        rts
@yes:   lda #1
        rts
@no:    lda #0
        rts

fly:
        ldy #E_VX
        jsr get_speed
        jsr move_x
        ldy #E_TIMER            ; y += jq_fly_dy[timer & 31]
        lda (ep),y
        and #31
        tax
        ldy #E_Y
        lda _jq_fly_dy,x
        bmi @neg
        clc
        adc (ep),y
        sta (ep),y
        iny
        lda (ep),y
        adc #0
        sta (ep),y
        jmp @bounds
@neg:   clc
        adc (ep),y
        sta (ep),y
        iny
        lda (ep),y
        adc #$FF
        sta (ep),y
@bounds:
        lda ex+1                ; x < 0 || x >= jq_level_w - 16: dir = -dir
        bmi @flip
        lda ex
        cmp flyw
        lda ex+1
        sbc flyw+1
        bcc @t
@flip:  jsr negdir
@t:     ldy #E_TIMER            ; timer > 120: dir = -dir, timer = 0
        lda (ep),y
        bmi @r
        cmp #121
        bcc @r
        lda #0
        sta (ep),y
        jsr negdir
@r:     rts

spider:
        ldy #E_VY
        jsr get_speed
        ldy #E_DIR
        lda (ep),y
        bmi @up
        ldy #E_Y                ; y += spd
        lda (ep),y
        clc
        adc spd
        sta (ep),y
        sta ey
        iny
        lda (ep),y
        adc #0
        sta (ep),y
        sta ey+1
        ldy #E_X                ; dropping: the floor, or timer > 100
        lda (ep),y
        sta tp_x
        iny
        lda (ep),y
        sta tp_x+1
        lda #14
        jsr tile_at_dy
        tax
        lda _jq_solid,x
        beq :+
        lda #$FF
        ldy #E_DIR
        sta (ep),y
:       ldy #E_TIMER
        lda (ep),y
        bmi @r
        cmp #101
        bcc @r
        lda #0
        sta (ep),y
        lda #$FF
        ldy #E_DIR
        sta (ep),y
@r:     rts
@up:    ldy #E_Y                ; y -= spd
        lda (ep),y
        sec
        sbc spd
        sta (ep),y
        iny
        lda (ep),y
        sbc #0
        sta (ep),y
        ldy #E_TIMER            ; climbing: timer > 60
        lda (ep),y
        bmi @r
        cmp #61
        bcc @r
        lda #0
        sta (ep),y
        lda #1
        ldy #E_DIR
        sta (ep),y
        rts
.endproc

; the column of ex + A (0..14), C's rounding towards zero; $FF off the map
.proc col_of
        clc
        adc ex
        sta qy
        lda ex+1
        adc #0
        bmi @neg
        cmp #16
        bcs @out
        lsr a
        ror qy
        lsr a
        ror qy
        lsr a
        ror qy
        lsr a
        ror qy
        lda qy
@chk:   cmp _tmap_w
        bcs @out
        rts
@neg:   cmp #$FF
        bne @out
        lda qy
        cmp #$F1
        bcc @out
        lda #0
        beq @chk
@out:   lda #$FF
        rts
.endproc

; enemies_rows(): the walkers' rows, after enemies_spawn()
_enemies_rows:
        lda _jq_level_w
        sec
        sbc #16
        sta flyw
        lda _jq_level_w+1
        sbc #0
        sta flyw+1
        lda #<(_game + ENEMIES)
        sta ep
        lda #>(_game + ENEMIES)
        sta ep+1
        ldx #0
@loop:  ldy #E_Y
        lda (ep),y
        sta ey
        iny
        lda (ep),y
        sta ey+1
        lda #$FF                ; no test yet
        sta ck_hi,x
        lda #0
        jsr row_of
        sta rm_lo,x
        tya
        sta rm_hi,x
        lda #12
        jsr row_of
        sta rf_lo,x
        tya
        sta rf_hi,x
        lda #16
        jsr row_of
        sta rb_lo,x
        tya
        sta rb_hi,x
        lda ep
        clc
        adc #ESIZE
        sta ep
        bcc :+
        inc ep+1
:       inx
        cpx #NENEMIES
        bne @loop
        rts

; the tmap row of ey + A: lo in A, hi in Y (0: off the map)
row_of: clc
        adc ey
        sta qy
        lda ey+1
        adc #0
        beq @pos
        cmp #$FF
        bne @out
        lda qy
        cmp #$F1
        bcc @out
        lda #0
        beq @row
@pos:   lda qy
        lsr a
        lsr a
        lsr a
        lsr a
@row:   cmp _tmap_h
        bcs @out
        asl a
        tay
        lda _tmap_row+1,y
        pha
        lda _tmap_row,y
        sta qy
        pla
        tay
        lda qy
        rts
@out:   lda #0
        tay
        rts

.proc negdir
        ldy #E_DIR
        lda #0
        sec
        sbc (ep),y
        sta (ep),y
        rts
.endproc

; x += or -= spd by dir; ex = the new x, ey = y
.proc move_x
        ldy #E_DIR
        lda (ep),y
        bmi @left
        ldy #E_X
        lda (ep),y
        clc
        adc spd
        sta (ep),y
        sta ex
        iny
        lda (ep),y
        adc #0
        sta (ep),y
        sta ex+1
        jmp @y
@left:  ldy #E_X
        lda (ep),y
        sec
        sbc spd
        sta (ep),y
        sta ex
        iny
        lda (ep),y
        sbc #0
        sta (ep),y
        sta ex+1
@y:     ldy #E_Y
        lda (ep),y
        sta ey
        iny
        lda (ep),y
        sta ey+1
        rts
.endproc
