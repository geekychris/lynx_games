; Bullion Dash - the tile lookup (level.c has the C): tile_at(ta_x, ta_y),
; out of the grid = solid
        .export _ta_x, _ta_y, _tile_at_g
        .import _tile_row

GRID_COLS  = 28
GRID_ROWS  = 16
TILE_SOLID = 2

        .bss
_ta_x:  .res 2
_ta_y:  .res 2
        .zeropage
tp:     .res 2
        .code
.proc _tile_at_g
        lda _ta_x+1
        ora _ta_y+1
        bne solid
        lda _ta_y
        cmp #GRID_ROWS
        bcs solid
        asl a
        tax
        lda _ta_x
        cmp #GRID_COLS
        bcs solid
        tay
        lda _tile_row,x
        sta tp
        lda _tile_row+1,x
        sta tp+1
        lda (tp),y
        ldx #0
        rts
solid:  lda #TILE_SOLID
        ldx #0
        rts
.endproc
