#!/bin/sh
# REV R1: the act files for the disc - G<list>S<pos>.MDG (md/sgb.h "SCMG") for every scene asked for,
# made by the PC host engine from the act as it starts (host/md_check.cpp, SCD32X_MDGEXPORT).
#   tools/build_mdg.sh <Data.rsdk> <out dir, e.g. game_data> <scenes: "1:0" or "1:0,1:1" or "1:*">
# "1:0@1" = scene 1:0 played as Tails (REV R3): its file is G1S128.MDG (pos + 128), with Tails's sprites.
# A scene without tile layers the Genesis can show gets no file (the console then shows it as before).
# Checks: each file fits the cart slot 0x380000-0x3EFFFF; SCDDATA.PAK (cart 0x300000) ends below 0x380000.
set -e
DATA=$(readlink -f "$1"); OUT=$(readlink -f "$2"); SCENES=${3:-1:0}
[ -f "$DATA" ] || { echo "data pack not found: $1"; exit 1; }
mkdir -p "$OUT"
cd "$(dirname "$0")/.."
make -s -C host
COUNT=$(python3 -c "
import sys; sys.path.insert(0,'tools')
import scd_report as sr
gc=sr.parse_gameconfig(sr.Data('$DATA').read('Data/Game/GameConfig.bin'))
print(' '.join('%d:%d'%(l,len(v)) for l,v in gc['lists'].items()))")
expand() { # "1:*" -> 1:0 1:1 ...; "1:0@1" -> 1:0@1
    for item in $(echo "$SCENES" | tr ',' ' '); do
        pl=0
        case "$item" in *@*) pl=${item#*@}; item=${item%@*} ;; esac
        l=${item%%:*}; p=${item#*:}
        if [ "$p" = "*" ]; then
            n=$(echo "$COUNT" | tr ' ' '\n' | sed -n "s/^$l://p")
            i=0; while [ "$i" -lt "${n:-0}" ]; do echo "$l:$i@$pl"; i=$((i + 1)); done
        else echo "$l:$p@$pl"; fi
    done
}
T=$(mktemp -d)
made=0
for s in $(expand); do
    pl=${s#*@}; s=${s%@*}
    l=${s%%:*}; p=${s#*:}
    case "$pl" in 0|1) ;; *) echo "*** scene $s@$pl: player 0 (Sonic) or 1 (Tails)"; exit 1 ;; esac
    rm -rf "$T/run" && mkdir -p "$T/run/mdg"
    printf "[Dev]\nStartingCategory=%s\nStartingScene=%s\nDataFile=%s\n" "$l" "$p" "$DATA" > "$T/run/settings.ini"
    # REV R4B: the chunks the act's scripts can place later (tile triggers ...) get their cells too
    placed=$(python3 tools/scd_chunks.py "$DATA" "$l" "$p")
    # REV R4C: the act's animated tiles (Copy16x16Tile) first - which tiles get which pictures (the act's timers: all
    # of them within its first seconds); the act file then has those pictures' cells (host/md_check.cpp)
    ( cd "$T/run" && SCD32X_PLAYER=$pl SCD32X_TILECOPYLOG="$T/run/copies.txt" SCD32X_MAX_FRAMES=900 \
        "$OLDPWD/host/soniccd_host" > pre.txt 2>&1 ) || true
    ( cd "$T/run" && SCD32X_PLAYER=$pl SCD32X_MDCHECK=1 SCD32X_MDGEXPORT="$T/run/mdg" SCD32X_MAX_FRAMES=120 SCD32X_EVENT_LOG=events.txt \
        SCD32X_PLACED="$placed" SCD32X_ANIMTILES="$T/run/copies.txt" "$OLDPWD/host/soniccd_host" > out.txt 2>&1 ) || true
    f=$(ls "$T/run/mdg"/*.MDG 2>/dev/null | head -1)
    name=$(printf "G%dS%03d.MDG" "$l" "$((p + 128 * pl))")
    if [ -z "$f" ]; then
        echo "  $name: no file (scene $s has no plane layers the check converted)"
        continue
    fi
    size=$(stat -c %s "$f")
    if [ "$size" -gt $((0x3F0000 - 0x380000)) ]; then
        echo "*** $name: $size bytes - more than the cart slot (458752)"; exit 1
    fi
    cp "$f" "$OUT/$name"
    ani=$(grep -o "animated tiles in [0-9]* chunks" "$T/run/events.txt" | head -1)
    echo "  $name: $size bytes ($(basename "$f" .MDG))${ani:+, $ani}"
    made=$((made + 1))
done
rm -rf "$T"
if [ -f "$OUT/SCDDATA.PAK" ]; then
    d=$(stat -c %s "$OUT/SCDDATA.PAK")
    if [ "$d" -gt $((0x380000 - 0x300000)) ]; then
        echo "*** SCDDATA.PAK is $d bytes: it would reach the act file's cart slot (0x380000) - fewer scenes"; exit 1
    fi
fi
echo "==> $made act files in $OUT"
