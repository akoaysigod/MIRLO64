#!/usr/bin/env python3
"""The software AI's output (the bench's AUDIO_RAW of a run on a core without MIRLO_AI) against the same algorithm run over the
buffers the game handed it (AIDUMP=<file> of an RSPDUMP=1 build): tools/ai_check_sw.py AUDIO_RAW AIDUMP STEP.
A raw word equal to the model's next sample is that sample; a zero word that is not is silence (the FIFO's lead, a dry AI). The
first word that is neither is reported: the buffers were changed after they were handed over (Pokemon Stadium) or the model is wrong."""
import struct, sys, math
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from ai_check import Soft, s16

raw = open(sys.argv[1], 'rb').read()
dump = open(sys.argv[2], 'rb').read()
step = int(sys.argv[3], 0)
out = struct.unpack('<%dI' % (len(raw) // 4), raw[:len(raw) // 4 * 4])
frames = list(struct.unpack('>%dI' % (len(dump) // 4), dump[:len(dump) // 4 * 4]))
soft = Soft(frames)
real = sil = 0
se = 0.0; mx = 0; bad = None
for k, w in enumerate(out):
    try:
        save = (soft.i, soft.have, soft.pl, soft.pr, soft.nl, soft.nr, soft.phase)
        want = soft.sample(step)
    except IndexError:
        break
    if w == want:
        real += 1
    elif w == 0:
        sil += 1
        soft.i, soft.have, soft.pl, soft.pr, soft.nl, soft.nr, soft.phase = save   # (silence: the model did not advance)
    else:
        for sh in (16, 0):
            d = s16(w >> sh) - s16(want >> sh); se += d * d; mx = max(mx, abs(d))
        real += 1
        if bad is None: bad = (k, w, want)
print("raw words %d: %d model samples, %d silence, frames used %d of %d; RMS %.3f max %d" %
      (len(out), real, sil, soft.i, len(frames), math.sqrt(se / max(1, 2 * real)), mx))
if bad: print("first difference: word %d got %08x want %08x" % bad)
print("ai_check_sw: " + ("same" if not bad else "DIFFERENT"))
sys.exit(1 if bad else 0)
