"""Synthesize SOL's short, descending science-fiction discharge as a mono 48 kHz one-shot."""
import math
from pathlib import Path
import random
import struct
import wave


RATE = 48000


def swept_phase(t, floor, fast, fast_decay, slow, slow_decay):
    return math.tau * (floor*t + fast*fast_decay*(-math.expm1(-t/fast_decay))
                       + slow*slow_decay*(-math.expm1(-t/slow_decay)))


def build_sound():
    rng = random.Random(56017)
    raw = []
    smooth_noise = 0.
    length = round(.213 * RATE)
    for i in range(length):
        t = i/RATE
        carrier = swept_phase(t,195.,510.,.012,615.,.044)
        wire = swept_phase(t,374.,965.,.0053,914.,.030)
        shimmer = swept_phase(t,673.,1190.,.0031,885.,.018)
        pick = 1.-math.exp(-t/.00064)
        body = math.sin(carrier + 1.62*math.exp(-t/.024)*math.sin(1.43*carrier+.35))
        twang = math.sin(wire + .74*math.exp(-t/.017)*math.sin(shimmer))
        noise = rng.uniform(-1,1)
        smooth_noise += .24*(noise-smooth_noise)
        spark = (noise-smooth_noise)*.23*math.exp(-t/.0031)
        pressure = math.sin(swept_phase(t,106.,84.,.017,95.,.026))*.13*math.exp(-t/.031)
        value = (.62*body*math.exp(-t/.034) + .33*twang*math.exp(-t/.024)
                 + .103*math.sin(shimmer)*math.exp(-t/.015) + pressure + spark)
        raw.append(pick*math.tanh(1.48*value))

    samples = raw[:]
    for seconds,gain in ((.0127,.123),(.0289,.068),(.0473,.029)):
        delay = round(seconds*RATE)
        for i in range(delay,length):
            samples[i] += gain*raw[i-delay]
    # A single bright onset stays distinct when several short tails overlap in automatic fire.
    previous = filtered = 0.
    for i,x in enumerate(samples):
        filtered = x-previous+.995*filtered
        previous = x
        samples[i] = filtered*min(1.,(length-1-i)/(.013*RATE))
    gain = .80/max(abs(x) for x in samples)
    pcm = struct.pack('<{}h'.format(length),*(round(x*gain*32767) for x in samples))
    path = Path(__file__).resolve().parents[3]/'ContentSource/ClassicRifle/S_SolBoltFire.wav'
    path.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(path),'wb') as wav:
        wav.setparams((1,2,RATE,length,'NONE','not compressed'))
        wav.writeframes(pcm)
    return path


if __name__ == '__main__':
    print(build_sound())
