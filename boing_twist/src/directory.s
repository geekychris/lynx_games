; made by tools/gen_data.py - do not edit
; The cartridge's directory: entry 0 the program (as cc65's defdir.s), then
; the data files (src/cart.s), each read by lynx_load(n) to its address.
        .include "lynx.inc"
        .import         __STARTOFDIRECTORY__
        .import         __MAIN_START__
        .import         __STARTUP_LOAD__, __BSS_LOAD__
        .import         __BANK0BLOCKSIZE__
        .import         _pic_buf, _room_buf, _texl
        .export         __DEFDIR__: absolute = 1

        .segment "DIRECTORY"

__DIRECTORY_START__:
off0 = __STARTOFDIRECTORY__ + (__DIRECTORY_END__ - __DIRECTORY_START__)
len0 = __BSS_LOAD__ - __STARTUP_LOAD__
        .byte   <(off0 / __BANK0BLOCKSIZE__)
        .word   off0 & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   __MAIN_START__
        .word   len0
; cart/pic_01.bin
        .byte   <((off0 + len0 + 0) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 0) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_02.bin
        .byte   <((off0 + len0 + 512) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 512) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_03.bin
        .byte   <((off0 + len0 + 1024) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 1024) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_04.bin
        .byte   <((off0 + len0 + 1536) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 1536) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_05.bin
        .byte   <((off0 + len0 + 2048) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 2048) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_06.bin
        .byte   <((off0 + len0 + 2560) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 2560) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_07.bin
        .byte   <((off0 + len0 + 3072) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 3072) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_08.bin
        .byte   <((off0 + len0 + 3584) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 3584) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_09.bin
        .byte   <((off0 + len0 + 4096) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 4096) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_10.bin
        .byte   <((off0 + len0 + 4608) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 4608) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_11.bin
        .byte   <((off0 + len0 + 5120) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 5120) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_12.bin
        .byte   <((off0 + len0 + 5632) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 5632) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_13.bin
        .byte   <((off0 + len0 + 6144) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 6144) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_14.bin
        .byte   <((off0 + len0 + 6656) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 6656) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_15.bin
        .byte   <((off0 + len0 + 7168) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 7168) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_16.bin
        .byte   <((off0 + len0 + 7680) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 7680) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/pic_17.bin
        .byte   <((off0 + len0 + 8192) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 8192) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _pic_buf
        .word   512
; cart/flip.bin
        .byte   <((off0 + len0 + 8704) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 8704) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   $E018
        .word   3289
; cart/room_00.bin
        .byte   <((off0 + len0 + 11993) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 11993) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   452
; cart/room_01.bin
        .byte   <((off0 + len0 + 12445) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 12445) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   456
; cart/room_02.bin
        .byte   <((off0 + len0 + 12901) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 12901) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   609
; cart/room_03.bin
        .byte   <((off0 + len0 + 13510) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13510) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/room_04.bin
        .byte   <((off0 + len0 + 13567) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13567) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/room_05.bin
        .byte   <((off0 + len0 + 13624) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13624) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/room_06.bin
        .byte   <((off0 + len0 + 13681) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13681) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/room_07.bin
        .byte   <((off0 + len0 + 13738) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13738) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/room_08.bin
        .byte   <((off0 + len0 + 13795) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13795) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/room_09.bin
        .byte   <((off0 + len0 + 13852) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13852) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _room_buf
        .word   57
; cart/title.bin
        .byte   <((off0 + len0 + 13909) / __BANK0BLOCKSIZE__)
        .word   (off0 + len0 + 13909) & (__BANK0BLOCKSIZE__ - 1)
        .byte   $88
        .word   _texl
        .word   1582
__DIRECTORY_END__:
