#!/usr/bin/env python3
"""REV R4: the game's audio for the disc - music as CD audio tracks, sound effects for the Sega CD's PCM chip.

  tools/build_audio.py <Data.rsdk> <game data dir> [--no-music] [--max-minutes M]

Music: the JP soundtrack and the past tracks (the game's own .ogg files, decoded by ffmpeg) become
<game data>/music/TrackNN.wav (44.1 kHz 16-bit stereo, track 2 on - track 1 is the data track), in the order
of MUSIC below. engine/CDTracks.inc tells the engine which track plays which file: each JP name and its US
twin play the same track (one soundtrack fits on the disc). tools/build_audio_disc.py puts the tracks on the disc.
--max-minutes: leave out the tracks no stage plays (the menus', the special stages') until the music fits.

Sound effects: KOBOSFX.BIN (Kobo's format, cd32x/sub_main.c pcm_load: 'KSFX', count, image bytes, 8 bytes an
effect, the 64 KB wave-RAM image at 0x100) and engine/SFXBank.inc (file name -> effect number). The PCM chip
plays 8-bit sign-magnitude samples from 64 KB; every effect gets a rate and a length (EFFECTS below), all rates
scaled down together until the image fits. Pitch: the chip's step 0x800 plays 32,552 Hz (12.5 MHz / 384); each
effect's step is rate * 2048 / 32552 rounded, and the sample is resampled to exactly the rate that step plays.

Made from the game's data: the outputs stay on your computer (not published)."""
import io, math, os, shutil, struct, subprocess, sys, wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk_pack as rp

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
# the disc's audio tracks, in order (track 2 on). "*" = needed by the stages; the others go first when space is short
MUSIC = ([("R%dB.ogg" % r, "*") for r in (1, 3, 4, 5, 6, 7, 8)] +
         [("JP/R%d%s.ogg" % (r, t), "*") for r in (1, 3, 4, 5, 6, 7, 8) for t in "ACD"] +
         [("JP/Boss.ogg", "*"), ("JP/FinalBoss.ogg", "*"), ("JP/Invincibility.ogg", "*"), ("JP/SpeedShoes.ogg", "*"),
          ("JP/GameOver.ogg", "*"), ("JP/ZoneComplete.ogg", "*"),
          ("JP/SpecialStage.ogg", ""), ("JP/TitleScreen.ogg", ""), ("JP/TimeAttack.ogg", ""), ("JP/DAGarden.ogg", "")])
# sound effects: (file in Data/SoundFX, rate Hz, longest seconds, volume 0-255). Effect 1 has a channel of its own
# (the ring: often several at once); the others share 7. The game's global effects - the stages' own come later.
EFFECTS = [("Global/Ring.wav", 11000, 0.40, 0xC0), ("Global/Jump.wav", 11000, 0.33, 0xC0),
           ("Global/Destroy.wav", 8000, 0.35, 0xE0), ("Global/Skidding.wav", 8000, 0.30, 0xB0),
           ("Global/Spring.wav", 8000, 0.40, 0xD0), ("Global/Hurt.wav", 8000, 0.50, 0xD0),
           ("Global/LoseRings.wav", 8000, 0.80, 0xD0), ("Global/Charge.wav", 8000, 0.70, 0xF0),
           ("Global/Release.wav", 8000, 0.80, 0xF0), ("Global/LampPost.wav", 8000, 0.40, 0xC0),
           ("Global/BlueShield.wav", 8000, 0.35, 0xC0), ("Global/Future.wav", 8000, 0.60, 0xD0),
           ("Global/Past.wav", 8000, 0.60, 0xD0), ("Global/TimeWarp.wav", 8000, 1.00, 0xD0),
           ("Global/SignPost.wav", 8000, 0.60, 0xC0), ("Global/ScoreAdd.wav", 8000, 0.10, 0xB0),
           ("Global/ScoreTotal.wav", 8000, 0.40, 0xC0), ("Global/Explosion.wav", 8000, 0.45, 0xE0),
           ("Global/1Up.wav", 8000, 0.42, 0xC0), ("Global/Flying.wav", 8000, 0.20, 0xA0),
           ("Global/Tired.wav", 8000, 0.20, 0xA0),
           # REV R4C: the game's own menus (the title, the main menu, save select, time attack): the cursor and the choice
           ("Global/MenuButton.wav", 8000, 0.20, 0xB0), ("Global/Select.wav", 8000, 0.35, 0xB0)]
PCM_CLOCK = 32552.0  # the RF5C164's rate at step 0x800 (12.5 MHz / 384)
PAGE, TAIL, RAM = 256, 16, 65536
MAX_FX = 63  # REV R4D: the Sub-CPU's table (sub_main.c SFX_MAX; R4C: 31 - KOBOSFX.BIN has 23, its header is 256 bytes as before)


def samples_of(data):
    """a WAV's samples as floats -1..1 (mono: channels averaged), its rate"""
    w = wave.open(io.BytesIO(data), "rb")
    ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    out = []
    for i in range(n):
        acc = 0.0
        for c in range(ch):
            o = (i * ch + c) * width
            if width == 1:
                v = (raw[o] - 128) / 128.0
            else:
                v = struct.unpack_from("<h", raw, o + width - 2)[0] / 32768.0
            acc += v
        out.append(acc / ch)
    return out, rate


def resample(a, src, dst):
    """src Hz -> dst Hz: down, each output sample the average of the source samples it stands for (a box filter -
    less aliasing hiss than picking one); up, linear"""
    n = max(1, int(len(a) * dst / src))
    out = []
    if dst < src:
        span = src / dst
        for i in range(n):
            k0 = int(i * span)
            k1 = max(k0 + 1, int((i + 1) * span))
            seg = a[k0:k1]
            out.append(sum(seg) / len(seg) if seg else 0.0)
        return out
    for i in range(n):
        t = i * src / dst
        k = int(t)
        f = t - k
        x0 = a[k] if k < len(a) else 0.0
        x1 = a[k + 1] if k + 1 < len(a) else x0
        out.append(x0 + (x1 - x0) * f)
    return out


def sign_magnitude(v):
    """-1..1 -> the chip's byte: bit 7 = positive, low 7 bits the size; 0xFF (the loop marker) never"""
    m = min(126, int(round(abs(v) * 126)))
    return (0x80 | m) if v >= 0 else m


def make_bank(pk, effects, min_rate_of=None):
    """the 64 KB wave-RAM image of these effects (name, rate Hz, longest seconds, volume): all rates scaled down
    together until it fits -> (KSFX blob, table, scale, left out). min_rate_of: (rate, count) - REV R4D: while the
    effects of 8000 Hz would play below rate, the last count effects are left out one at a time (a scene's long list).
    The blob: 'KSFX', u16 effects n, u16 image bytes (0: 64 KB), n x 8 bytes, zeros to the next 256 bytes, the image"""
    clips = []
    for name, rate, secs, vol in effects:
        d = pk.read("Data/SoundFX/" + name)
        if d is None:
            print("  (no Data/SoundFX/%s - left out)" % name)
            continue
        a, r = samples_of(d)
        a = a[:int(secs * r)]
        fade = min(len(a), int(0.01 * r))  # a short fade: nothing clicks
        for i in range(fade):
            a[len(a) - 1 - i] *= i / fade
        # REV R4B: each effect to full scale (at most x4), the chip's volume (EFFECTS) sets the balance - quiet ones
        # (the spin dash: peaks at a third of full scale) were lost under the CD music
        peak = max((abs(v) for v in a), default=0.0)
        if peak > 0:
            g = min(0.98 / peak, 4.0)
            a = [v * g for v in a]
        clips.append((name, a, r, rate, vol))
    dropped = []
    while True:
        scale = 1.0
        while True:
            total, plan = 0, []
            for name, a, r, rate, vol in clips:
                fd = max(1, int(round(rate * scale * 2048 / PCM_CLOCK)))
                play = fd * PCM_CLOCK / 2048  # the rate the chip plays at this step: the sample is made at exactly it
                n = max(1, int(len(a) * play / r))
                need = ((n + TAIL + 1 + PAGE - 1) // PAGE) * PAGE
                plan.append((name, a, r, fd, play, vol, n))
                total += need
            if total <= RAM and len(plan) <= MAX_FX:
                break
            if total <= RAM:  # (too many: only dropping helps)
                scale = 0.0
                break
            scale *= 0.99  # (fine steps: as much of the 64 KB as fits)
        if not min_rate_of or (scale * 8000 >= min_rate_of[0] and scale > 0) or min_rate_of[1] <= 0:
            break
        dropped.append(clips.pop()[0])  # the list's last (a stage effect)
        min_rate_of = (min_rate_of[0], min_rate_of[1] - 1)
    image, table = bytearray(), []
    for name, a, r, fd, play, vol, n in plan:
        page = len(image) // PAGE
        s = resample(a, r, play)[:n]
        start = len(image)
        image += bytes(sign_magnitude(v) for v in s)
        loop = len(image)  # the silence after it: a finished effect loops silence
        image += bytes([0x80]) * TAIL + b"\xff"
        image += b"\xff" * ((-len(image)) % PAGE)
        table.append((page, vol, fd, loop & 0xFFFF, (loop - start) & 0xFFFF, name, play, len(s) / play))
    assert len(image) <= RAM and len(table) <= MAX_FX
    blob = bytearray(b"KSFX" + struct.pack(">HH", len(table), len(image) & 0xFFFF))
    for page, vol, fd, loop, length, name, play, dur in table:
        blob += struct.pack(">BBHHH", page, vol, fd, loop, length)
    blob += bytes((-len(blob)) % 0x100)  # (the Sub-CPU: the image at (8 + n * 8 + 255) & ~255)
    blob += image
    return blob, table, scale, dropped


# REV R4D: the scenes' own banks (the Options Menu's STAGE SFX switch; off: KOBOSFX.BIN everywhere, as R4C). Each scene
# gets the shared effects its kind of scene plays and its own (StageConfig.bin), at most STAGE_SECS[list] each; a scene
# with a long list (the sound test's) leaves its last ones out while the rates would fall below MIN_RATE. Scenes with
# the same list share a bank: SFX01.BIN, SFX02.BIN ... (the Sub-CPU's command 53 with the bank number). In the acts the
# longest shared effects are cut shorter than in KOBOSFX.BIN (room for the stage's own).
ACT_CUT = {"Global/TimeWarp.wav": 0.70, "Global/LoseRings.wav": 0.60, "Global/Release.wav": 0.60, "Global/Charge.wav": 0.55}
ACT_SHARED = [(n, r, ACT_CUT.get(n, s), v) for n, r, s, v in EFFECTS if n not in ("Global/MenuButton.wav", "Global/Select.wav")] + [
    ("Global/SpecialRing.wav", 8000, 0.60, 0xD0), ("Global/SpecialWarp.wav", 8000, 0.60, 0xD0)]
SS_SHARED = [("Global/Ring.wav", 11000, 0.40, 0xC0), ("Global/Jump.wav", 11000, 0.33, 0xC0), ("Global/ScoreAdd.wav", 8000, 0.10, 0xB0),
             ("Global/ScoreTotal.wav", 8000, 0.40, 0xC0), ("Global/Explosion.wav", 8000, 0.45, 0xE0),
             ("Global/1Up.wav", 8000, 0.42, 0xC0), ("Global/SpecialWarp.wav", 8000, 0.80, 0xD0)]
MENU_SHARED = [("Global/MenuButton.wav", 8000, 0.20, 0xB0), ("Global/Select.wav", 8000, 0.35, 0xB0)]
STAGE_RATE, STAGE_VOL, MIN_RATE = 8000, 0xD0, 4000
STAGE_SECS = {0: 3.0, 1: 0.40, 3: 1.20}  # the menus' few effects (the title's "SEGA") whole; the acts' cut short


def scene_lists(pk):
    """GameConfig.bin's stage lists -> {list: [(folder, its StageConfig's effect names)]}"""
    import scd_report as sr
    gc = sr.parse_gameconfig(pk.read("Data/Game/GameConfig.bin"))
    out = {}
    for cat in (0, 1, 3):
        rows = []
        for folder, sid, name in gc["lists"].get(cat, []):
            d = pk.read("Data/Stages/%s/StageConfig.bin" % folder)
            sfx = list(dict.fromkeys(sr.parse_stageconfig(d)[2])) if d else []
            rows.append((folder, sfx))
        out[cat] = rows
    return out


def build_sfx(pk, out_dir):
    blob, table, scale, _ = make_bank(pk, EFFECTS)
    open(os.path.join(out_dir, "KOBOSFX.BIN"), "wb").write(blob)
    print("KOBOSFX.BIN: %d effects, %d of %d bytes of wave RAM (rates x %.2f)" % (len(table), len(blob) - 0x100, RAM, scale))
    for k, (page, vol, fd, loop, length, name, play, dur) in enumerate(table):
        print("  %2d %-24s %6.0f Hz (step 0x%04X)  %.2f s" % (k + 1, name, play, fd, dur))
    # REV R4D: the scenes' banks
    for f in os.listdir(out_dir):
        if f.startswith("SFX") and f.endswith(".BIN"):
            os.remove(os.path.join(out_dir, f))
    banks, bank_tables, scene_bank, notes = {}, [table], {}, []
    for cat, rows in scene_lists(pk).items():
        shared = ACT_SHARED if cat == 1 else SS_SHARED if cat == 3 else MENU_SHARED
        for pos, (folder, sfx) in enumerate(rows):
            secs = STAGE_SECS[cat] if len(sfx) <= 8 else STAGE_SECS[1]  # (a long list: as an act's)
            effs = list(shared) + [(n, STAGE_RATE, secs, STAGE_VOL) for n in sfx if n not in [e[0] for e in shared]]
            key = tuple(effs)
            if key not in banks:
                if len(banks) >= 99:
                    scene_bank[(cat, pos)] = 0
                    continue
                b, t, sc, dropped = make_bank(pk, effs, (MIN_RATE, len(effs) - len(shared)))
                n = len(banks) + 1
                open(os.path.join(out_dir, "SFX%02d.BIN" % n), "wb").write(b)
                banks[key] = n
                bank_tables.append(t)
                notes.append("SFX%02d.BIN: %s (%d:%d) - %d effects, rates x %.2f (%.0f Hz)%s" % (
                    n, folder, cat, pos, len(t), sc, 8000 * sc, "; left out: " + ", ".join(dropped) if dropped else ""))
            scene_bank[(cat, pos)] = banks[key]
    names = []
    for t in bank_tables:
        for row in t:
            if row[5] not in names:
                names.append(row[5])
    with open(os.path.join(ROOT, "engine", "SFXBank.inc"), "w") as f:
        f.write("// REV R4: the sound effects in KOBOSFX.BIN (tools/build_audio.py) - made from the game's data, not published\n")
        f.write("static const ScdSfx scdSfxBank[] = {\n")
        for k, (page, vol, fd, loop, length, name, play, dur) in enumerate(table):
            f.write('    { "%s", %d }, /* %.0f Hz (step 0x%04X), %.2f s */\n' % (name, k + 1, play, fd, dur))
        f.write("};\nstatic const int scdSfxBankCount = %d;\n" % len(table))
        f.write("// REV R4D: every scene's own bank (SFX<nn>.BIN; 0: KOBOSFX.BIN) - the effects' names, the scenes' banks,\n")
        f.write("// each bank's effect number of each name (0: not in it)\n")
        f.write("#define SCD_SFX_BANKS %d\n#define SCD_SFX_NAMES %d\n" % (len(bank_tables), len(names)))
        f.write("static const char *const scdSfxNames[SCD_SFX_NAMES] = {\n")
        for n in names:
            f.write('    "%s",\n' % n)
        f.write("};\nstatic const unsigned char scdSfxScene[4][0x80] = {\n")
        for cat in range(4):
            vals = [scene_bank.get((cat, pos), 0) for pos in range(0x80)]
            f.write("    { %s },\n" % ", ".join(str(v) for v in vals))
        f.write("};\nstatic const unsigned char scdSfxBankMap[SCD_SFX_BANKS][SCD_SFX_NAMES] = {\n")
        for t in bank_tables:
            ids = {row[5]: k + 1 for k, row in enumerate(t)}
            f.write("    { %s },\n" % ", ".join(str(ids.get(n, 0)) for n in names))
        f.write("};\n")
    print("scene banks: %d files (SFX01.BIN - SFX%02d.BIN), %d effect names" % (len(banks), len(banks), len(names)))
    for n in notes:
        print("  " + n)


def build_music(pk, out_dir, max_minutes):
    ff = shutil.which("ffmpeg")
    if not ff:
        sys.exit("ffmpeg is needed to decode the game's music: sudo apt-get install -y ffmpeg")
    mdir = os.path.join(out_dir, "music")
    os.makedirs(mdir, exist_ok=True)
    for f in os.listdir(mdir):
        if f.startswith("Track") and f.endswith(".wav"):
            os.remove(os.path.join(mdir, f))
    tmp = os.path.join(mdir, "decode.ogg")
    tracks = []
    for name, need in MUSIC:
        d = pk.read("Data/Music/" + name)
        if d is None:
            print("  (no Data/Music/%s)" % name)
            continue
        tracks.append((name, need, d))
    # how long each one is decoded (the PCM length decides)
    pcm = []
    for name, need, d in tracks:
        open(tmp, "wb").write(d)
        r = subprocess.run([ff, "-v", "error", "-i", tmp, "-ar", "44100", "-ac", "2", "-f", "s16le", "-"], capture_output=True)
        if r.returncode:
            sys.exit("ffmpeg could not decode %s: %s" % (name, r.stderr.decode(errors="replace")[:300]))
        pcm.append((name, need, r.stdout))
    os.remove(tmp)
    total = sum(len(p) for _, _, p in pcm) / (44100 * 4) / 60 + len(pcm) * 2 / 60
    while max_minutes and total > max_minutes:
        drop = next((k for k in range(len(pcm) - 1, -1, -1) if not pcm[k][1]), None)
        if drop is None:
            break
        print("  left out (no stage plays it; the disc is full): %s" % pcm[drop][0])
        del pcm[drop]
        total = sum(len(p) for _, _, p in pcm) / (44100 * 4) / 60 + len(pcm) * 2 / 60
    rows = []
    for k, (name, need, p) in enumerate(pcm):
        n = k + 2
        w = wave.open(os.path.join(mdir, "Track%02d.wav" % n), "wb")
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(p)
        w.close()
        secs = len(p) / (44100 * 4)
        rows.append((name, n, int(math.ceil(secs))))
    with open(os.path.join(ROOT, "engine", "CDTracks.inc"), "w") as f:
        f.write("// REV R4: the disc's audio tracks (tools/build_audio.py) - made from the game's data, not published\n")
        f.write("static const ScdTrack scdTracks[] = {\n")
        for name, n, secs in rows:
            f.write('    { "Data/Music/%s", %d, %d },\n' % (name, n, secs))
            if name.startswith("JP/"):
                f.write('    { "Data/Music/US/%s", %d, %d },\n' % (name[3:], n, secs))
        f.write("};\nstatic const int scdTrackCount = %d;\n" % (len(rows) + sum(1 for r in rows if r[0].startswith("JP/"))))
    print("music: %d tracks (2 - %d), %.1f minutes with the 2-second gaps -> %s" % (len(rows), len(rows) + 1, total, mdir))


def main():
    args = sys.argv[1:]
    music, max_minutes = True, 0.0
    if "--no-music" in args:
        args.remove("--no-music")
        music = False
    if "--max-minutes" in args:
        i = args.index("--max-minutes")
        max_minutes = float(args[i + 1])
        del args[i:i + 2]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    pk = rp.Pack(os.path.abspath(args[0]))
    out_dir = os.path.abspath(args[1])
    os.makedirs(out_dir, exist_ok=True)
    build_sfx(pk, out_dir)
    if music:
        build_music(pk, out_dir, max_minutes)


if __name__ == "__main__":
    main()
