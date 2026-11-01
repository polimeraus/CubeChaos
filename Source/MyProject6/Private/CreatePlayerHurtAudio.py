"""Short edits of the human effort recordings already included in this project."""
from pathlib import Path
import array, json, math, wave

root=Path(__file__).resolve().parents[3]
out=root/'ContentSource/PlayerFeedback'
report=[]
for index, duration in enumerate((.32,.34,.29),1):
    source=out/'Original'/f'FengMao_Effort_Pain_{index:02}.wav'
    with wave.open(str(source),'rb') as audio:
        assert audio.getsampwidth()==2
        rate,channels=audio.getframerate(),audio.getnchannels()
        raw=array.array('h',audio.readframes(audio.getnframes()))
    mono=[sum(raw[i:i+channels])/(channels*32768) for i in range(0,len(raw),channels)]
    window=int(rate*.005)
    onset=next((i for i in range(0,len(mono)-window,window)
        if math.sqrt(sum(v*v for v in mono[i:i+window])/window)>.025),0)
    start=max(0,onset-int(rate*.008))
    samples=mono[start:start+int(rate*duration)]
    # Remove rumble and DC; retain the natural human voice, with no electronic tone.
    previous=0.; filtered=0.; pole=math.exp(-2*math.pi*65/rate)
    for i,value in enumerate(samples):
        filtered=pole*(filtered+value-previous); previous=value
        attack=min(1.,i/(rate*.006))
        tail=min(1.,(len(samples)-1-i)/(rate*.075))
        samples[i]=filtered*attack*(.5-.5*math.cos(math.pi*tail))
    peak=max(map(abs,samples))
    samples=[v*.68/peak for v in samples]
    name=f'S_PlayerHurt_{index:02}'
    with wave.open(str(out/(name+'.wav')),'wb') as audio:
        audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(rate)
        audio.writeframes(array.array('h',[round(v*32767) for v in samples]).tobytes())
    report.append({'name':name,'source_asset':f'/Game/NPC/ParagonFengMao/Characters/Heroes/FengMao/Sounds/SoundWaves/FengMao_Effort_Pain_{index:02}',
        'duration_seconds':len(samples)/rate,'channels':1,'sample_rate':rate,'peak':max(map(abs,samples)),
        'rms':math.sqrt(sum(v*v for v in samples)/len(samples)),'clipped_samples':sum(abs(v)>=1 for v in samples)})
(out/'AudioProvenance.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
