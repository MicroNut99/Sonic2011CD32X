#!/usr/bin/env python3
# PORT32X D8: a .cue with our data track 1 and the PC disc's audio tracks 2.. (same numbers, so the
# game's CDPlay(n) plays the PC disc's track n). The audio is not copied: the cue points at the PC disc's
# .bin file(s) (paths relative to the new .cue). Usage: make_mixed_cue.py <our.bin> <pc.cue> <out.cue>
import sys, os, re
ours, pccue, out = sys.argv[1:4]
outdir = os.path.dirname(os.path.abspath(out))
lines = ['FILE "%s" BINARY' % os.path.basename(ours), '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00']
cur_file, pending, ntracks, in_audio = None, None, 0, False
for raw in open(pccue, encoding='latin-1').read().splitlines():
    l = raw.strip()
    m = re.match(r'FILE\s+"(.+)"\s+(\w+)', l)
    if m:
        p = os.path.join(os.path.dirname(os.path.abspath(pccue)), m.group(1))
        cur_file = 'FILE "%s" %s' % (os.path.relpath(p, outdir).replace('\\', '/'), m.group(2))
        pending = cur_file
        continue
    m = re.match(r'TRACK\s+(\d+)\s+(\S+)', l)
    if m:
        in_audio = m.group(2).upper() == 'AUDIO'
        if in_audio:
            if pending: lines.append(pending); pending = None
            lines.append('  TRACK %02d AUDIO' % int(m.group(1))); ntracks += 1
        continue
    if in_audio and (l.startswith('INDEX') or l.startswith('PREGAP') or l.startswith('POSTGAP')):
        lines.append('    ' + l)
open(out, 'w', newline='\r\n').write('\n'.join(lines) + '\n')
print('%s: data track + %d audio tracks from %s' % (out, ntracks, pccue))
