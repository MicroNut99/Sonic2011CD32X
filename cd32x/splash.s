! PORT32X: the splash screen data for sh2_splash.c (tools/make_splash.py -> build/SPLASH.SPL)
        .section .rodata
        .align  4
        .global _splash
_splash:
        .incbin "build/SPLASH.SPL"
