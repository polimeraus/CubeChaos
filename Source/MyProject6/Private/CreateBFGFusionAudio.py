"""Original layered fusion cannon audio; mono 48 kHz with headroom and no clipped samples."""
from pathlib import Path
import math, random, wave, struct, json
root=Path(__file__).resolve().parents[3]
out=root/'ContentSource/BFGFusion';out.mkdir(parents=True,exist_ok=True)
rate=48000
stats={}
for name,duration,impact in [('S_BFGFusionFire',.48,False),('S_BFGFusionImpact',.68,True)]:
    rng=random.Random(715 if impact else 714)
    samples=[];low=0.;mid=0.;previous=0.;phase=0.;metalphase=0.
    for i in range(int(rate*duration)):
        t=i/rate;n=rng.uniform(-1,1)
        low+=.045*(n-low);mid+=.32*(n-mid)
        high=n-mid
        # A fast, hard initial crack layered over a pitched pressure thump.
        freq=(48+112*math.exp(-t*28)) if impact else (62+210*math.exp(-t*38))
        phase+=2*math.pi*freq/rate
        body=(math.sin(phase)+.28*math.sin(phase*2.01))*math.exp(-t*(8 if impact else 13))
        crack=(.65*high+.55*mid)*math.exp(-t*(48 if impact else 95))
        grit=math.tanh(low*9)*math.exp(-t*(10 if impact else 21))
        # Inharmonic energy discharge: metallic and raspy rather than a pure whistle.
        metalphase+=2*math.pi*(260+950*math.exp(-t*25))/rate
        zap=(math.sin(metalphase+2.6*math.sin(metalphase*1.41))*.45+mid*.5)*math.exp(-t*20)
        x=(body*.8+crack*1.15+grit*(.85 if impact else .6)+zap*.55)
        x=math.tanh(x*1.3)*min(1,t/.0007)
        samples.append(x)
    dry=samples[:]
    for delay,gain in [(0.023,.16),(.047,.10),(.079,.065)]:
        shift=int(delay*rate)
        for i in range(shift,len(samples)):samples[i]+=dry[i-shift]*gain
    # Short fade makes voice termination and repeated fire click-free.
    for i in range(len(samples)):
        samples[i]*=min(1,(len(samples)-1-i)/(rate*.035))
    peak=max(abs(v) for v in samples)
    samples=[v*.92/peak for v in samples]
    pcm=b''.join(struct.pack('<h',round(v*32767)) for v in samples)
    with wave.open(str(out/(name+'.wav')),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(pcm)
    stats[name]={'duration':duration,'rate':rate,'peak':max(abs(v) for v in samples),'rms':math.sqrt(sum(v*v for v in samples)/len(samples)),'clipped_samples':sum(abs(v)>=1 for v in samples)}
(out/'AudioStats.json').write_text(json.dumps(stats,indent=2))
print(json.dumps(stats,indent=2))
