; Bullion Dash - the level decoder's binary arithmetic decoding step in
; 6502 (gridpack.c; the reference is the ZX Spectrum SDK's gridpack.c):
;
;   bound = (rng >> 8) * p; if code < bound: rng = bound, b = 0
;   else code -= bound, rng -= bound, b = 1
;   while rng < $8000: rng <<= 1, code = code << 1 | next bit

        .export _gp_decode, _gp_next_bit, _gp_bp, _gp_left, _gp_rng, _gp_code

        .zeropage
_gp_bp:  .res 2
        .bss
_gp_left: .res 1
_gp_rng:  .res 2
_gp_code: .res 2
cur:     .res 1
bound:   .res 2
pp:      .res 1
        .code

; unsigned char gp_next_bit(void): the next bit of the stream, MSB first
.proc _gp_next_bit
        lda _gp_left
        bne @have
        ldy #0
        lda (_gp_bp),y
        sta cur
        inc _gp_bp
        bne @n
        inc _gp_bp+1
@n:     lda #8
        sta _gp_left
@have:  dec _gp_left
        lda #0
        asl cur
        rol a
        ldx #0
        rts
.endproc

; unsigned char __fastcall__ gp_decode(unsigned char p): one decision, p = P(0) / 256
.proc _gp_decode
        sta pp
        ; bound = (rng >> 8) * p  (8 x 8 -> 16, shift-add)
        lda #0
        sta bound+1
        lda _gp_rng+1
        sta bound               ; the multiplicand, shifted out as we go
        lda #0
        ldx #8
@m:     lsr bound
        bcc @s
        clc
        adc pp
@s:     ror a
        ror bound+1
        dex
        bne @m
        ; A:bound+1 = product (high:low)... realign: product = A * 256 + bound+1
        sta tmp
        lda bound+1
        sta bound
        lda tmp
        sta bound+1
        ; code < bound ?
        lda _gp_code
        cmp bound
        lda _gp_code+1
        sbc bound+1
        bcs one
        lda bound
        sta _gp_rng
        lda bound+1
        sta _gp_rng+1
        lda #0
        beq norm
one:    sec
        lda _gp_code
        sbc bound
        sta _gp_code
        lda _gp_code+1
        sbc bound+1
        sta _gp_code+1
        sec
        lda _gp_rng
        sbc bound
        sta _gp_rng
        lda _gp_rng+1
        sbc bound+1
        sta _gp_rng+1
        lda #1
norm:   sta result
@loop:  lda _gp_rng+1
        bmi @done
        asl _gp_rng
        rol _gp_rng+1
        jsr _gp_next_bit
        lsr a                   ; the bit into C
        rol _gp_code
        rol _gp_code+1
        jmp @loop
@done:  lda result
        ldx #0
        rts
        .bss
tmp:    .res 1
result: .res 1
.endproc
