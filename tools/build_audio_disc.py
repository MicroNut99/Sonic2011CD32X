#!/usr/bin/env python3
"""REV R4: the game disc with its music - the data track the Makefile made, then the CD audio tracks.

  tools/build_audio_disc.py <SCDENG.bin> <music dir> [--out NAME]

<SCDENG.bin>: cd32x/build_engine/SCDENG.bin, the data track as 2352-byte sectors (make ENGINE=1 writes it with its
.cue). <music dir>: game_data/music, Track02.wav, Track03.wav ... (tools/build_audio.py: 44.1 kHz 16-bit stereo).
Writes NAME.bin and NAME.cue (default: SCDENG_AUDIO next to SCDENG.bin): track 1 the data track unchanged, track 2
on the music - the track numbers engine/CDTracks.inc plays. Track 2 has the 2-second gap a data track needs before
audio (INDEX 00 / 01); the others follow straight on. Burn NAME.cue.
Says how full the disc is: an 80-minute CD-R holds 79:59, a 74-minute one 74:00."""
import os, sys, wave

SECTOR = 2352


def msf(s):
    return "%02d:%02d:%02d" % (s // 4500, s // 75 % 60, s % 75)


def main():
    args = sys.argv[1:]
    out = None
    if "--out" in args:
        i = args.index("--out")
        out = args[i + 1]
        del args[i:i + 2]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    data, mdir = os.path.abspath(args[0]), os.path.abspath(args[1])
    size = os.path.getsize(data)
    if size % SECTOR:
        sys.exit("%s: %d bytes is not whole 2352-byte sectors - not the Makefile's SCDENG.bin" % (data, size))
    wavs = []
    n = 2
    while os.path.exists(os.path.join(mdir, "Track%02d.wav" % n)):
        wavs.append(os.path.join(mdir, "Track%02d.wav" % n))
        n += 1
    if not wavs:
        sys.exit("no Track02.wav in %s - run tools/build_audio.py first" % mdir)
    out = out or os.path.join(os.path.dirname(data), "SCDENG_AUDIO")
    binname = os.path.basename(out) + ".bin"
    pos = size // SECTOR
    cue = ['FILE "%s" BINARY' % binname, "  TRACK 01 MODE1/2352", "    INDEX 01 00:00:00"]
    with open(out + ".bin", "wb") as fb:
        with open(data, "rb") as fd:
            while True:
                b = fd.read(1 << 22)
                if not b:
                    break
                fb.write(b)
        for k, wp in enumerate(wavs):
            w = wave.open(wp, "rb")
            if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (2, 2, 44100):
                sys.exit("%s: not 44.1 kHz 16-bit stereo - made by tools/build_audio.py?" % wp)
            pcm = w.readframes(w.getnframes())
            w.close()
            pcm += bytes((-len(pcm)) % SECTOR)
            cue.append("  TRACK %02d AUDIO" % (k + 2))
            if k == 0:  # the gap after the data track
                fb.write(bytes(150 * SECTOR))
                cue.append("    INDEX 00 %s" % msf(pos))
                pos += 150
            cue.append("    INDEX 01 %s" % msf(pos))
            fb.write(pcm)
            pos += len(pcm) // SECTOR
    with open(out + ".cue", "w", newline="") as fc:
        fc.write("\r\n".join(cue) + "\r\n")
    mins = (pos + 150) / 4500.0  # with the lead-in's 2 seconds
    print("%s.bin + .cue: data track %d sectors, %d audio tracks (2 - %d), %s in all" % (
        out, size // SECTOR, len(wavs), len(wavs) + 1, msf(pos)))
    if mins > 79.9:
        print("*** %.1f minutes: more than an 80-minute CD-R holds - build_audio.py --max-minutes leaves tracks out" % mins)
        sys.exit(1)
    print("fits an 80-minute CD-R" + ("" if mins <= 74 else " (not a 74-minute one)"))


if __name__ == "__main__":
    main()
