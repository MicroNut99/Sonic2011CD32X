# R4D – the second SH2, each stage's own sound effects, the underwater colours

**Goal of the R4D disc:** three changes to the boot stack on one disc, each behind its own switch in the Options Menu.
**All three are off at power-on: with them off the disc plays as R4C** (the same pictures, sounds and loads).

| Switch | What it does | Boot stack part |
|---|---|---|
| `2ND SH2` | the 32X's second SH2 draws half of every picture of the 32X picture mode (the special stages, the title, the menus) | the SH2 loader (`cd32x/sh2_engboot.c`, `SCDSH2.BIN`): its `secondary()` waits for work from the engine |
| `STAGE SFX` | every scene loads its own sound effects into the PCM chip (the stage's own: bumpers, flippers, the drowning alarm ...) | the Sub-CPU (`cd32x/sub_main.c`): command 53 takes a bank number |
| `UNDERWATER` | the water colours below the water line (Tidal Tempest): the Genesis planes and sprites | the 68000 listener (`cd32x/kobo/hw_md.s`): a line interrupt (H-int) at the water line |

**No new command numbers.** Command 53 (load the sound effects) reads COMM2 as the bank: 0 is `KOBOSFX.BIN`, as in
R4C (R4C's engine always sent 0).

## The Options Menu

A + B + C + Start, as before. Three rows before `CREDITS`, each `OFF` at power-on; A, Start, Left or Right switch one:

```
PLAYER     SONIC
START GAME
STAGE SELECT
LIVES      3
DEMO PAD   OFF
DEBUG MODE OFF
2ND SH2    OFF
STAGE SFX  OFF
UNDERWATER OFF
CREDITS
```

The menu starts higher on the screen (the two blank rows under `OPTIONS MENU` and the version are gone) so the 26 rows
fit. A switch stays as set until the power goes off; `START GAME` or `STAGE SELECT` then plays with it.

- `2ND SH2` works from the next picture.
- `STAGE SFX` works from the next scene the game loads (the scene's bank is loaded with it).
- `UNDERWATER` works from the next act the game loads (the act's first frame sends the 68000 all its colours).

PLACEHOLDER
