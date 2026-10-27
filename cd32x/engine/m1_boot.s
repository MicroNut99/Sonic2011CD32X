! SONIC R6 EDCD (CART_ED=1 only): the engine image's first bytes for a CARTRIDGE boot.
! .text.m1stub (cart 0x02000000): an SH2 stub to scd_engine_entry - the boot loader jumps to the
! image's first byte (sh2_engboot.c ENGINE_ENTRY).
! .m1jt (cart 0x200): the 68000 jump table the 32X vector ROM sends every 68000 vector to in a
! cartridge boot.  The engine overwrites the launcher's image in the EverDrive, so the table must
! come back with the engine (vectors 1-47, jmp abs.l each, as tools/prep_rom.py --mode1 and
! edboot/make_edcd.py write it in the CD32X BootCamp: into the RAM jump table at 0xFFFD00).
! In a CD boot nothing reads either (the vectors are the BIOS's): the CART_ED disc boots both ways.

        .section .text.m1stub,"ax"
        mov.l   1f,r0
        jmp     @r0
        nop
        .align  2
1:      .long   _scd_engine_entry

        .section .m1jt,"a"
        .align  2
! each entry is 6 bytes (jmp abs.l), so the .long halves go as two words
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 1 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 2 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFD7E  ! vector 3 -> 0xFFFD7E
        .word   0x4EF9, 0x00FF, 0xFD7E  ! vector 4 -> 0xFFFD7E
        .word   0x4EF9, 0x00FF, 0xFD84  ! vector 5 -> 0xFFFD84
        .word   0x4EF9, 0x00FF, 0xFD78  ! vector 6 -> 0xFFFD78
        .word   0x4EF9, 0x00FF, 0xFD8A  ! vector 7 -> 0xFFFD8A
        .word   0x4EF9, 0x00FF, 0xFD9C  ! vector 8 -> 0xFFFD9C
        .word   0x4EF9, 0x00FF, 0xFDA2  ! vector 9 -> 0xFFFDA2
        .word   0x4EF9, 0x00FF, 0xFD90  ! vector 10 -> 0xFFFD90
        .word   0x4EF9, 0x00FF, 0xFD96  ! vector 11 -> 0xFFFD96
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 12 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 13 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 14 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 15 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 16 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 17 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 18 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 19 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 20 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 21 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 22 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 23 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 24 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 25 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFD12  ! vector 26 -> 0xFFFD12
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 27 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFD0C  ! vector 28 -> 0xFFFD0C
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 29 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFD06  ! vector 30 -> 0xFFFD06
        .word   0x4EF9, 0x00FF, 0xFDA8  ! vector 31 -> 0xFFFDA8
        .word   0x4EF9, 0x00FF, 0xFD18  ! vector 32 -> 0xFFFD18
        .word   0x4EF9, 0x00FF, 0xFD1E  ! vector 33 -> 0xFFFD1E
        .word   0x4EF9, 0x00FF, 0xFD24  ! vector 34 -> 0xFFFD24
        .word   0x4EF9, 0x00FF, 0xFD2A  ! vector 35 -> 0xFFFD2A
        .word   0x4EF9, 0x00FF, 0xFD30  ! vector 36 -> 0xFFFD30
        .word   0x4EF9, 0x00FF, 0xFD36  ! vector 37 -> 0xFFFD36
        .word   0x4EF9, 0x00FF, 0xFD3C  ! vector 38 -> 0xFFFD3C
        .word   0x4EF9, 0x00FF, 0xFD42  ! vector 39 -> 0xFFFD42
        .word   0x4EF9, 0x00FF, 0xFD48  ! vector 40 -> 0xFFFD48
        .word   0x4EF9, 0x00FF, 0xFD4E  ! vector 41 -> 0xFFFD4E
        .word   0x4EF9, 0x00FF, 0xFD54  ! vector 42 -> 0xFFFD54
        .word   0x4EF9, 0x00FF, 0xFD5A  ! vector 43 -> 0xFFFD5A
        .word   0x4EF9, 0x00FF, 0xFD60  ! vector 44 -> 0xFFFD60
        .word   0x4EF9, 0x00FF, 0xFD66  ! vector 45 -> 0xFFFD66
        .word   0x4EF9, 0x00FF, 0xFD6C  ! vector 46 -> 0xFFFD6C
        .word   0x4EF9, 0x00FF, 0xFD72  ! vector 47 -> 0xFFFD72
