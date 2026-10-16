| PORT32X: round S3b - the SH2 program, built into APP.BIN (as Kobo's files.s)
        .text
        .align  4
        .global sh2_app_start
sh2_app_start:
        .incbin "SCDSH2.BIN"   | S6b3: found through -I$(B) - each build folder embeds its own SH2 program
        .align  4
sh2_app_end:
        .global sh2_app_length
sh2_app_length:
        .long   sh2_app_end - sh2_app_start
