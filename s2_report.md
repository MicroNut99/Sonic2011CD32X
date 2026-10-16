# SonicCD 32X – round S2 report

Pack: 929 files. Bytecode: PC (GS000.bin + RSnnn.bin).

## 1. Palette lines (Mega Drive: 4 lines of 15 colours, shared by planes and sprites)

Colours reduced to the Mega Drive's 9 bits. Tiles = the act's unique 8x8 cells. Sprites = connected areas of the Global, Players and zone sprite sheets (about one sprite frame each), coloured through this stage's palette. Tile lines = optimised packing (best of 200 tries) with its lower bound; + sprites = lines after adding the sprites while keeping 4 lines; Left over = sprite areas that do not fit (candidates for the 32X layer or for palette work), with their sheets.

| Stage | Tile lines | Lower bound | Lines with sprites | Left over | Sheets of the left-overs |
|---|---:|---:|---:|---:|---|
| Secrets/Act2 | 0 | 0 | 4 | 16 | Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif, Global/Pause_DE.gif … |
| Secrets/Act3 | 0 | 0 | 4 | 16 | Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif, Global/Pause_DE.gif … |
| R11A/Act1 | 3 | 2 | 4 | 27 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R11B/Act1 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R11C/Act1 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R11D/Act1 | 2 | 2 | 4 | 30 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R12A/Act2 | 3 | 2 | 4 | 25 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R12B/Act2 | 2 | 2 | 4 | 27 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R12C/Act2 | 3 | 2 | 4 | 29 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R12D/Act2 | 2 | 2 | 4 | 30 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R13C/Act3 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R13D/Act3 | 2 | 2 | 4 | 30 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R31A/Act1 | 2 | 2 | 4 | 43 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R31B/Act1 | 2 | 2 | 4 | 45 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R31C/Act1 | 2 | 2 | 4 | 46 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R31D/Act1 | 3 | 2 | 4 | 54 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R32A/Act2 | 2 | 2 | 4 | 44 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R32B/Act2 | 2 | 2 | 4 | 46 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R32C/Act2 | 2 | 2 | 4 | 46 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R32D/Act2 | 2 | 2 | 4 | 46 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R33C/Act3 | 2 | 2 | 4 | 49 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R33D/Act3 | 3 | 2 | 4 | 53 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R41A/Act1 | 2 | 2 | 4 | 31 | Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif, Global/Pause_DE.gif … |
| R41B/Act1 | 2 | 2 | 4 | 22 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R41C/Act1 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R41D/Act1 | 2 | 2 | 4 | 24 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R42A/Act2 | 2 | 2 | 4 | 23 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R42B/Act2 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R42C/Act2 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R42D/Act2 | 2 | 2 | 4 | 24 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R43C/Act3 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R43D/Act3 | 2 | 2 | 4 | 24 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R51A/Act1 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R51B/Act1 | 3 | 2 | 4 | 26 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R51C/Act1 | 3 | 2 | 4 | 26 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R51D/Act1 | 3 | 2 | 4 | 23 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R52A/Act2 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R52B/Act2 | 3 | 2 | 4 | 26 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R52C/Act2 | 3 | 2 | 4 | 26 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R52D/Act2 | 2 | 2 | 4 | 25 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R53C/Act3 | 2 | 2 | 4 | 34 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R53D/Act3 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R61A/Act1 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R61B/Act1 | 3 | 2 | 4 | 40 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R61C/Act1 | 2 | 2 | 4 | 23 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R61D/Act1 | 2 | 2 | 4 | 31 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R62A/Act2 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R62B/Act2 | 3 | 2 | 4 | 55 | Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R62C/Act2 | 2 | 2 | 4 | 23 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R62D/Act2 | 2 | 2 | 4 | 31 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R63C/Act3 | 2 | 2 | 4 | 27 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R63D/Act3 | 2 | 2 | 4 | 29 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R71A/Act1 | 3 | 3 | 4 | 25 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R71B/Act1 | 3 | 3 | 4 | 28 | Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R71C/Act1 | 3 | 3 | 4 | 30 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R71D/Act1 | 3 | 2 | 4 | 31 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R72A/Act2 | 3 | 3 | 4 | 25 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R72B/Act2 | 3 | 3 | 4 | 28 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R72C/Act2 | 3 | 3 | 4 | 30 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R72D/Act2 | 3 | 2 | 4 | 31 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R73C/Act3 | 3 | 3 | 4 | 30 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R73D/Act3 | 2 | 2 | 4 | 31 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R81A/Act1 | 2 | 2 | 4 | 26 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R81B/Act1 | 3 | 3 | 4 | 26 | Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R81C/Act1 | 3 | 2 | 4 | 27 | Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R81D/Act1 | 2 | 2 | 4 | 25 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R82A/Act2 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R82B/Act2 | 2 | 2 | 4 | 25 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R82C/Act2 | 3 | 3 | 4 | 27 | Global/DPad.gif, Global/Display.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif … |
| R82D/Act2 | 2 | 2 | 4 | 28 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R83C/Act3 | 3 | 2 | 4 | 49 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| R83D/Act3 | 2 | 2 | 4 | 48 | Global/DPad.gif, Global/Display_t.gif, Global/Items.gif, Global/Items2.gif, Global/Items2_t.gif, Global/Items3.gif … |
| SS1/Act1 | 2 | 2 | 2 | 0 |  |
| SS2/Act2 | 3 | 2 | 3 | 0 |  |
| SS3/Act3 | 3 | 2 | 3 | 0 |  |
| SS4/Act4 | 4 | 2 | 4 | 0 |  |
| SS5/Act5 | 2 | 2 | 2 | 0 |  |
| SS6/Act6 | 3 | 2 | 3 | 0 |  |
| SS7/Act7 | 3 | 2 | 3 | 0 |  |
| SS8/Act8 | 2 | 2 | 2 | 0 |  |

## 2. Music (CD audio tracks)

| Track | Seconds |
|---|---:|
| JP/Boss.ogg | 82.0 |
| JP/DAGarden.ogg | 122.0 |
| JP/FinalBoss.ogg | 81.4 |
| JP/GameOver.ogg | 11.4 |
| JP/Invincibility.ogg | 21.9 |
| JP/R1A.ogg | 113.1 |
| JP/R1C.ogg | 85.5 |
| JP/R1D.ogg | 88.9 |
| JP/R3A.ogg | 101.7 |
| JP/R3C.ogg | 73.2 |
| JP/R3D.ogg | 81.3 |
| JP/R4A.ogg | 122.2 |
| JP/R4C.ogg | 84.7 |
| JP/R4D.ogg | 82.3 |
| JP/R5A.ogg | 100.1 |
| JP/R5C.ogg | 83.2 |
| JP/R5D.ogg | 65.9 |
| JP/R6A.ogg | 91.8 |
| JP/R6C.ogg | 63.5 |
| JP/R6D.ogg | 77.6 |
| JP/R7A.ogg | 119.9 |
| JP/R7C.ogg | 77.1 |
| JP/R7D.ogg | 71.9 |
| JP/R8A.ogg | 86.3 |
| JP/R8C.ogg | 63.8 |
| JP/R8D.ogg | 70.6 |
| JP/SpecialStage.ogg | 122.0 |
| JP/SpeedShoes.ogg | 22.6 |
| JP/TimeAttack.ogg | 82.8 |
| JP/TitleScreen.ogg | 30.0 |
| JP/ZoneComplete.ogg | 8.3 |
| R1B.ogg | 92.3 |
| R3B.ogg | 79.0 |
| R4B.ogg | 72.2 |
| R5B.ogg | 77.2 |
| R6B.ogg | 105.3 |
| R7B.ogg | 80.9 |
| R8B.ogg | 76.1 |
| US/Boss.ogg | 83.4 |
| US/DAGarden.ogg | 121.6 |
| US/FinalBoss.ogg | 82.9 |
| US/GameOver.ogg | 10.7 |
| US/Invincibility.ogg | 23.5 |
| US/R1A.ogg | 112.7 |
| US/R1C.ogg | 81.5 |
| US/R1D.ogg | 95.6 |
| US/R3A.ogg | 98.3 |
| US/R3C.ogg | 83.6 |
| US/R3D.ogg | 83.0 |
| US/R4A.ogg | 141.2 |
| US/R4C.ogg | 114.8 |
| US/R4D.ogg | 108.1 |
| US/R5A.ogg | 115.2 |
| US/R5C.ogg | 64.0 |
| US/R5D.ogg | 106.1 |
| US/R6A.ogg | 158.2 |
| US/R6C.ogg | 58.7 |
| US/R6D.ogg | 88.3 |
| US/R7A.ogg | 98.1 |
| US/R7C.ogg | 90.3 |
| US/R7D.ogg | 82.1 |
| US/R8A.ogg | 91.6 |
| US/R8C.ogg | 70.5 |
| US/R8D.ogg | 81.7 |
| US/SpecialStage.ogg | 135.4 |
| US/SpeedShoes.ogg | 23.0 |
| US/TitleScreen.ogg | 28.6 |
| US/ZoneComplete.ogg | 8.4 |

| Set | Tracks | Minutes |
|---|---:|---:|
| JP | 31 | 39.8 |
| US | 30 | 42.3 |
| shared | 7 | 9.7 |

All music: 68 tracks, 91.9 min, plus 2 s gap per track (2.3 min). A CD holds 74 min (80 min on 700 MB discs) and at most 99 tracks; the data track takes about 1 min per 9 MB (150 KB/s).

## 3. Sound effects

Sizes at 22,050 Hz 8-bit (the slave SH2's PWM mixer, read from the cart). The data is 44,100 Hz 8-bit mono.

Global sound effects (GameConfig, always loaded): 28 files, 725 KB at 22 kHz.

| Stage | Stage sfx | KB at 22 kHz | With global KB |
|---|---:|---:|---:|
| Title/Act1 | 1 | 35 | 761 |
| Menu/Act1 | 2 | 58 | 784 |
| TAttack/Act1 | 1 | 6 | 731 |
| Secrets/Act1 | 42 | 782 | 1507 |
| Secrets/Act2 | 42 | 782 | 1507 |
| DAgdn/Act1 | 0 | 0 | 725 |
| Help/Act1 | 0 | 0 | 725 |
| Credits/Act1 | 0 | 0 | 725 |
| Secrets/Act3 | 42 | 782 | 1507 |
| R11A/Act1 | 5 | 89 | 814 |
| R11B/Act1 | 5 | 106 | 831 |
| R11C/Act1 | 5 | 106 | 831 |
| R11D/Act1 | 5 | 106 | 831 |
| R12A/Act2 | 5 | 106 | 831 |
| R12B/Act2 | 5 | 106 | 831 |
| R12C/Act2 | 5 | 106 | 831 |
| R12D/Act2 | 5 | 106 | 831 |
| R13C/Act3 | 1 | 11 | 737 |
| R13D/Act3 | 1 | 11 | 737 |
| R31A/Act1 | 10 | 165 | 890 |
| R31B/Act1 | 8 | 104 | 829 |
| R31C/Act1 | 8 | 104 | 829 |
| R31D/Act1 | 8 | 104 | 829 |
| R32A/Act2 | 9 | 122 | 848 |
| R32B/Act2 | 9 | 122 | 848 |
| R32C/Act2 | 9 | 122 | 848 |
| R32D/Act2 | 9 | 122 | 848 |
| R33C/Act3 | 9 | 122 | 848 |
| R33D/Act3 | 9 | 122 | 848 |
| R41A/Act1 | 11 | 223 | 948 |
| R41B/Act1 | 11 | 223 | 948 |
| R41C/Act1 | 11 | 223 | 948 |
| R41D/Act1 | 11 | 223 | 948 |
| R42A/Act2 | 11 | 223 | 948 |
| R42B/Act2 | 11 | 223 | 948 |
| R42C/Act2 | 11 | 223 | 948 |
| R42D/Act2 | 11 | 223 | 948 |
| R43C/Act3 | 11 | 224 | 949 |
| R43D/Act3 | 11 | 224 | 949 |
| R51A/Act1 | 4 | 81 | 806 |
| R51B/Act1 | 4 | 81 | 806 |
| R51C/Act1 | 5 | 90 | 815 |
| R51D/Act1 | 5 | 90 | 815 |
| R52A/Act2 | 4 | 81 | 806 |
| R52B/Act2 | 5 | 90 | 815 |
| R52C/Act2 | 5 | 90 | 815 |
| R52D/Act2 | 5 | 90 | 815 |
| R53C/Act3 | 6 | 212 | 937 |
| R53D/Act3 | 6 | 212 | 937 |
| R61A/Act1 | 10 | 99 | 824 |
| R61B/Act1 | 10 | 99 | 824 |
| R61C/Act1 | 10 | 99 | 824 |
| R61D/Act1 | 10 | 99 | 824 |
| R62A/Act2 | 10 | 99 | 824 |
| R62B/Act2 | 10 | 99 | 824 |
| R62C/Act2 | 10 | 99 | 824 |
| R62D/Act2 | 10 | 99 | 824 |
| R63C/Act3 | 13 | 193 | 918 |
| R63D/Act3 | 13 | 193 | 918 |
| R71A/Act1 | 3 | 25 | 750 |
| R71B/Act1 | 3 | 25 | 750 |
| R71C/Act1 | 3 | 25 | 750 |
| R71D/Act1 | 3 | 25 | 750 |
| R72A/Act2 | 3 | 25 | 750 |
| R72B/Act2 | 3 | 25 | 750 |
| R72C/Act2 | 3 | 25 | 750 |
| R72D/Act2 | 3 | 25 | 750 |
| R73C/Act3 | 9 | 189 | 915 |
| R73D/Act3 | 9 | 189 | 915 |
| R81A/Act1 | 2 | 73 | 798 |
| R81B/Act1 | 2 | 73 | 798 |
| R81C/Act1 | 2 | 73 | 798 |
| R81D/Act1 | 2 | 73 | 798 |
| R82A/Act2 | 6 | 128 | 853 |
| R82B/Act2 | 6 | 128 | 853 |
| R82C/Act2 | 6 | 128 | 853 |
| R82D/Act2 | 6 | 128 | 853 |
| R83C/Act3 | 10 | 214 | 939 |
| R83D/Act3 | 10 | 214 | 939 |
| SS1/Act1 | 8 | 129 | 854 |
| SS2/Act2 | 8 | 129 | 854 |
| SS3/Act3 | 8 | 129 | 854 |
| SS4/Act4 | 8 | 129 | 854 |
| SS5/Act5 | 8 | 129 | 854 |
| SS6/Act6 | 8 | 129 | 854 |
| SS7/Act7 | 8 | 129 | 854 |
| SS8/Act8 | 8 | 129 | 854 |

Biggest sound effects (seconds at their own rate):

| File | Seconds |
|---|---:|
| Global/TimeWarp.wav | 6.19 |
| Stage/BombCarrier.wav | 4.86 |
| Global/Achievement.wav | 3.98 |
| Stage/Crumble.wav | 2.69 |
| Stage/Event.wav | 2.45 |
| Stage/MSCharge.wav | 2.14 |
| Global/Charge.wav | 2.10 |
| Global/Release.wav | 2.04 |
| Stage/ScoreChute.wav | 2.02 |
| Global/SignPost.wav | 2.00 |
| Global/SpecialWarp.wav | 1.84 |
| Global/ScoreTotal.wav | 1.75 |

## 4. Scripts

The engine expands bytecode to 4-byte ints (scriptCode + jumpTable). Global scripts: 34554 + 2385 ints = 144 KB. The engine's arrays hold 256K + 16K ints (1,088 KB).

| Stage | Bytecode file | Ints | KB | With global KB |
|---|---|---:|---:|---:|
| Title/Act1 | PS000.bin | 3961 | 15 | 160 |
| Menu/Act1 | PS001.bin | 24389 | 95 | 240 |
| TAttack/Act1 | PS002.bin | 9938 | 39 | 183 |
| Secrets/Act1 | PS003.bin | 5555 | 22 | 166 |
| Secrets/Act2 | PS004.bin | 5555 | 22 | 166 |
| DAgdn/Act1 | PS005.bin | 8822 | 34 | 179 |
| Help/Act1 | PS006.bin | 3229 | 13 | 157 |
| Credits/Act1 | PS007.bin | 2766 | 11 | 155 |
| Secrets/Act3 | PS008.bin | 5555 | 22 | 166 |
| R11A/Act1 | RS000.bin | 25164 | 98 | 243 |
| R11B/Act1 | RS001.bin | 20307 | 79 | 224 |
| R11C/Act1 | RS002.bin | 17825 | 70 | 214 |
| R11D/Act1 | RS003.bin | 17247 | 67 | 212 |
| R12A/Act2 | RS004.bin | 18045 | 70 | 215 |
| R12B/Act2 | RS005.bin | 17362 | 68 | 212 |
| R12C/Act2 | RS006.bin | 18045 | 70 | 215 |
| R12D/Act2 | RS007.bin | 17467 | 68 | 213 |
| R13C/Act3 | RS008.bin | 20942 | 82 | 226 |
| R13D/Act3 | RS009.bin | 20364 | 80 | 224 |
| R31A/Act1 | RS010.bin | 17641 | 69 | 213 |
| R31B/Act1 | RS011.bin | 14925 | 58 | 203 |
| R31C/Act1 | RS012.bin | 15334 | 60 | 204 |
| R31D/Act1 | RS013.bin | 14673 | 57 | 202 |
| R32A/Act2 | RS014.bin | 16992 | 66 | 211 |
| R32B/Act2 | RS015.bin | 16779 | 66 | 210 |
| R32C/Act2 | RS016.bin | 16977 | 66 | 211 |
| R32D/Act2 | RS017.bin | 16038 | 63 | 207 |
| R33C/Act3 | RS018.bin | 14358 | 56 | 200 |
| R33D/Act3 | RS019.bin | 14373 | 56 | 200 |
| R41A/Act1 | RS020.bin | 22455 | 88 | 232 |
| R41B/Act1 | RS021.bin | 22872 | 89 | 234 |
| R41C/Act1 | RS022.bin | 23808 | 93 | 237 |
| R41D/Act1 | RS023.bin | 23320 | 91 | 235 |
| R42A/Act2 | RS024.bin | 24000 | 94 | 238 |
| R42B/Act2 | RS025.bin | 24536 | 96 | 240 |
| R42C/Act2 | RS026.bin | 26185 | 102 | 247 |
| R42D/Act2 | RS027.bin | 25697 | 100 | 245 |
| R43C/Act3 | RS028.bin | 17973 | 70 | 214 |
| R43D/Act3 | RS029.bin | 17035 | 67 | 211 |
| R51A/Act1 | RS030.bin | 30210 | 118 | 262 |
| R51B/Act1 | RS031.bin | 30001 | 117 | 261 |
| R51C/Act1 | RS032.bin | 32779 | 128 | 272 |
| R51D/Act1 | RS033.bin | 32575 | 127 | 272 |
| R52A/Act2 | RS034.bin | 29932 | 117 | 261 |
| R52B/Act2 | RS035.bin | 30225 | 118 | 262 |
| R52C/Act2 | RS036.bin | 31379 | 123 | 267 |
| R52D/Act2 | RS037.bin | 31175 | 122 | 266 |
| R53C/Act3 | RS038.bin | 29096 | 114 | 258 |
| R53D/Act3 | RS039.bin | 28568 | 112 | 256 |
| R61A/Act1 | RS040.bin | 29812 | 116 | 261 |
| R61B/Act1 | RS041.bin | 24562 | 96 | 240 |
| R61C/Act1 | RS042.bin | 37356 | 146 | 290 |
| R61D/Act1 | RS043.bin | 37898 | 148 | 292 |
| R62A/Act2 | RS044.bin | 24805 | 97 | 241 |
| R62B/Act2 | RS045.bin | 24805 | 97 | 241 |
| R62C/Act2 | RS046.bin | 24310 | 95 | 239 |
| R62D/Act2 | RS047.bin | 24084 | 94 | 238 |
| R63C/Act3 | RS048.bin | 24746 | 97 | 241 |
| R63D/Act3 | RS049.bin | 24235 | 95 | 239 |
| R71A/Act1 | RS050.bin | 22375 | 87 | 232 |
| R71B/Act1 | RS051.bin | 22287 | 87 | 231 |
| R71C/Act1 | RS052.bin | 22247 | 87 | 231 |
| R71D/Act1 | RS053.bin | 21699 | 85 | 229 |
| R72A/Act2 | RS054.bin | 22216 | 87 | 231 |
| R72B/Act2 | RS055.bin | 20140 | 79 | 223 |
| R72C/Act2 | RS056.bin | 22088 | 86 | 231 |
| R72D/Act2 | RS057.bin | 21540 | 84 | 228 |
| R73C/Act3 | RS058.bin | 18845 | 74 | 218 |
| R73D/Act3 | RS059.bin | 19609 | 77 | 221 |
| R81A/Act1 | RS060.bin | 14675 | 57 | 202 |
| R81B/Act1 | RS061.bin | 14089 | 55 | 199 |
| R81C/Act1 | RS062.bin | 14393 | 56 | 201 |
| R81D/Act1 | RS063.bin | 14454 | 56 | 201 |
| R82A/Act2 | RS064.bin | 26009 | 102 | 246 |
| R82B/Act2 | RS065.bin | 25048 | 98 | 242 |
| R82C/Act2 | RS066.bin | 25074 | 98 | 242 |
| R82D/Act2 | RS067.bin | 25074 | 98 | 242 |
| R83C/Act3 | RS068.bin | 27154 | 106 | 250 |
| R83D/Act3 | RS069.bin | 26588 | 104 | 248 |
| SS1/Act1 | SS000.bin | 19854 | 78 | 222 |
| SS2/Act2 | SS001.bin | 17662 | 69 | 213 |
| SS3/Act3 | SS002.bin | 17787 | 69 | 214 |
| SS4/Act4 | SS003.bin | 17779 | 69 | 214 |
| SS5/Act5 | SS004.bin | 17816 | 70 | 214 |
| SS6/Act6 | SS005.bin | 19219 | 75 | 219 |
| SS7/Act7 | SS006.bin | 17700 | 69 | 213 |
| SS8/Act8 | SS007.bin | 17631 | 69 | 213 |

## 5. Cart budget per act, all time periods loaded together (4 MB = 4,096 KB)

Per period: Mega Drive tiles (cells x 32 bytes), chunk table, collision in the engine's form (138 KB), layouts, stage scripts. Shared: sprites at 4 bits (bounding boxes of the sprite areas of Global + Players + zone), global + stage sound effects at 22 kHz, global scripts.

| Act | Periods | Per-period KB (sum) | Sprites KB | Sfx KB | Global scripts KB | Total KB | % of cart |
|---|---|---:|---:|---:|---:|---:|---:|
| R11 | ABCD | 1364 | 581 | 840 | 144 | 2929 | 71 |
| R12 | ABCD | 1332 | 581 | 831 | 144 | 2888 | 71 |
| R13 | CD | 674 | 581 | 737 | 144 | 2135 | 52 |
| R31 | ABCD | 1286 | 581 | 890 | 144 | 2902 | 71 |
| R32 | ABCD | 1301 | 581 | 848 | 144 | 2874 | 70 |
| R33 | CD | 620 | 581 | 848 | 144 | 2193 | 54 |
| R41 | ABCD | 1372 | 579 | 948 | 144 | 3043 | 74 |
| R42 | ABCD | 1406 | 579 | 948 | 144 | 3077 | 75 |
| R43 | CD | 632 | 579 | 949 | 144 | 2304 | 56 |
| R51 | ABCD | 1520 | 578 | 806 | 144 | 3049 | 74 |
| R52 | ABCD | 1511 | 578 | 806 | 144 | 3040 | 74 |
| R53 | CD | 722 | 578 | 937 | 144 | 2382 | 58 |
| R61 | ABCD | 1538 | 618 | 824 | 144 | 3124 | 76 |
| R62 | ABCD | 1424 | 618 | 824 | 144 | 3010 | 73 |
| R63 | CD | 696 | 618 | 918 | 144 | 2376 | 58 |
| R71 | ABCD | 1376 | 582 | 750 | 144 | 2853 | 70 |
| R72 | ABCD | 1396 | 582 | 750 | 144 | 2872 | 70 |
| R73 | CD | 658 | 582 | 915 | 144 | 2299 | 56 |
| R81 | ABCD | 1246 | 583 | 798 | 144 | 2772 | 68 |
| R82 | ABCD | 1421 | 583 | 853 | 144 | 3001 | 73 |
| R83 | CD | 712 | 583 | 939 | 144 | 2378 | 58 |
