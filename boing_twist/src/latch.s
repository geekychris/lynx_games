; latch.s - presses latched in the vertical blank: the game's frames can take
; two or three display frames (drawing the ball), and a tap shorter than that
; would otherwise be missed. main.c input() takes the latch with the state now.

        .export         _in_latch
        .interruptor    latch_vbl

        .include        "lynx.inc"

        .bss
_in_latch:      .res    2               ; JOYSTICK bits, PAUSE as bit 8 (lynxinput.h)

        .code
latch_vbl:
        lda     INTSET
        and     #$04                    ; timer 2 = vertical blank
        beq     @out
        lda     JOYSTICK
        tsb     _in_latch
        lda     SWITCHES
        and     #$01
        tsb     _in_latch+1
@out:   clc
        rts
