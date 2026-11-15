; Bullion Dash - the guards' pathfinding search in 6502 (gridbfs.c calls it;
; the C it replaces is the ZX Spectrum SDK's gridbfs.c host version):
;
; gb_run: take cells off the queue (a ring of 2-byte cell offsets in one
; 256-byte page, gb_qhead/gb_qtail byte indexes) until it is empty or the
; wanted cell has its distance; label each neighbour the cell may be left
; to (L, R, U, D) and that can be entered, and queue it.
;
; gb_clear(carry): every distance unknown (255); with carry, cells known to
; have no path (>= FAR) keep FAR.
;
; port (Lynx, speed): the same search, ~40% fewer cycles: the four
; neighbours are read through one pointer to the cell above (Y offsets 0,
; STRIDE-2, STRIDE+2, 2*STRIDE: the grid's row stride is a constant here),
; the visits are inline, and gb_clear without carry is a page-wise fill.

        .export _gb_run, _gb_clear
        .import _gb_cells, _gb_stride2, _gb_qhead, _gb_qtail, _gb_want
        .import _gb_searched, _gb_overflow, _gb_queue

GB_L    = $01
GB_R    = $02
GB_U    = $04
GB_D    = $08
GB_IN   = $10
FAR     = 253
NCELLS  = 29 * 18               ; GB_CELLS(28, 16) / 2

STRIDE  = 2 * 29                ; gb_stride2 for Bullion Dash's 28 columns

        .zeropage
bp:     .res 2                  ; the cell above the one taken
mo:     .res 2                  ; the cell taken, as an offset into the cells
vp:     .res 2                  ; (gb_clear)
qp:     .res 2                  ; the queue's page
wp:     .res 2                  ; the wanted cell
        .bss
dist:   .res 1
        .code

; the neighbour at bp+YOFF may be left by BIT into the cell taken: label it
; dist; queue it (offset mo+DELTA) if it can be entered
.macro  visit   YOFF, BIT, DELTA
        .local  skip
        ldy #YOFF
        lda (bp),y
        and #BIT
        beq skip
        iny
        lda (bp),y
        cmp #255
        bne skip
        lda dist
        sta (bp),y
        dey
        lda (bp),y
        and #GB_IN
        beq skip
        ldy _gb_qtail
        clc
        lda mo
        adc #<(DELTA)
        sta (qp),y
        iny
        lda mo+1
        adc #>(DELTA)
        sta (qp),y
        iny
        sty _gb_qtail
        cpy _gb_qhead
        bne skip
        lda #1
        sta _gb_overflow
skip:
.endmacro

.proc _gb_run
        lda _gb_queue
        sta qp
        lda _gb_queue+1
        sta qp+1
        lda _gb_want
        sta wp
        lda _gb_want+1
        sta wp+1
loop:   lda _gb_qhead
        cmp _gb_qtail
        beq done
        ldy #1
        lda (wp),y
        cmp #255
        bne done
        ldy _gb_qhead           ; mo = queue[qhead]; bp = cells + mo - STRIDE
        lda (qp),y
        sta mo
        sec
        sbc #<STRIDE
        tax
        iny
        lda (qp),y
        sta mo+1
        sbc #>STRIDE
        iny
        sty _gb_qhead
        tay
        clc
        txa
        adc _gb_cells
        sta bp
        tya
        adc _gb_cells+1
        sta bp+1
        inc _gb_searched
        bne @s
        inc _gb_searched+1
@s:     ldy #STRIDE+1
        lda (bp),y
        clc
        adc #1
        sta dist
        cmp #FAR
        bne go
        lda _gb_qtail
        sta _gb_qhead
done:   rts
go:     visit STRIDE+2, GB_L, 2          ; the cell to the right goes left into it
        visit STRIDE-2, GB_R, -2
        visit 2*STRIDE, GB_U, STRIDE     ; the cell below climbs up
        visit 0, GB_D, -STRIDE           ; the cell above falls
        jmp loop
.endproc

NBYTES  = 2 * NCELLS

; void __fastcall__ gb_clear(unsigned char carry)
.proc _gb_clear
        sta carry
        clc
        lda _gb_cells
        adc #1
        sta vp
        lda _gb_cells+1
        adc #0
        sta vp+1
        lda carry
        bne slow
        ldx #>NBYTES            ; whole pages, then the rest
        lda #255
@p:     ldy #0
@l:     sta (vp),y
        iny
        iny
        bne @l
        inc vp+1
        dex
        bne @p
        ldy #0
@r:     sta (vp),y
        iny
        iny
        cpy #<NBYTES
        bcc @r
        rts
slow:   lda #<NCELLS
        sta n
        lda #>NCELLS
        sta n+1
        ldy #0
@l:     lda (vp),y
        cmp #FAR
        bcc @u
        lda #FAR
        bne @w
@u:     lda #255
@w:     sta (vp),y
        clc
        lda vp
        adc #2
        sta vp
        bcc @n
        inc vp+1
@n:     lda n
        bne @d
        dec n+1
@d:     dec n
        lda n
        ora n+1
        bne @l
        rts
        .bss
carry:  .res 1
n:      .res 2
.endproc
