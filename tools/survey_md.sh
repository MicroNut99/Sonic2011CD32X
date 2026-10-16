#!/bin/sh
# REV R0: run every regular act (scenes 0-69 of the regular list) through the host engine with the
# Mega Drive plane model (tools/run_md_check.sh) and collect one line per act.
#   tools/survey_md.sh <Data.rsdk> [frames per act, default 1800] > survey.txt
DATA=${1:?usage: tools/survey_md.sh Data.rsdk [frames]}
FRAMES=${2:-1800}
cd "$(dirname "$0")/.."
for i in $(seq 0 69); do
    sh tools/run_md_check.sh "$DATA" $i $FRAMES > runs/survey_$i.txt 2>&1
    python3 tools/survey_line.py $i runs/survey_$i.txt
done
