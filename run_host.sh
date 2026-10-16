#!/bin/bash
# PORT32X: run the PC host build on the game data in gamedata/ (Data.rsdk goes there).
#   ./run_host.sh [frames] [more engine args, e.g. console=true]
# Writes into gamedata/run/: PNG frames (every 15th), crc.txt, events.txt.
cd "$(dirname "$0")/gamedata" || { echo "make a folder gamedata/ with Data.rsdk in it"; exit 1; }
[ -f Data.rsdk ] || { echo "gamedata/Data.rsdk not found"; exit 1; }
FRAMES=${1:-600}; shift
rm -rf run && mkdir run
SCD32X_MAX_FRAMES=$FRAMES SCD32X_FRAMES_DIR=run SCD32X_SAVE_EVERY=${SCD32X_SAVE_EVERY:-15} \
SCD32X_CRC_LOG=run/crc.txt SCD32X_EVENT_LOG=run/events.txt \
  ../host/soniccd_host console=true "$@" > run/console.txt 2>&1
echo "exit code $? - $(ls run/*.png 2>/dev/null | wc -l) frames saved in gamedata/run/"
tail -n 3 run/events.txt
