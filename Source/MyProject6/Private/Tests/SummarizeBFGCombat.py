"""Compare actual PIE CSV frame captures (run with ordinary Python)."""
import csv, json, statistics
from pathlib import Path
root=Path(__file__).resolve().parents[4]
metrics=['FrameTime','GameThreadTime','GPUTime','Exclusive/GameThread/TimerManager','Exclusive/GameThread/TickActors','Exclusive/GameThread/EndOfFrameUpdates','GPU/Translucency','GPU/TranslucentLighting']
report={'method':'Same saved level, rendered PIE at 1280x720, audio disabled, NPC movement frozen, 0.25s between shots. Engine CSV timings in milliseconds; no isolated mesh-only timings.','phases':{}}
for phase in ['Idle','Flight','FlightNoLights','FlightNoVisuals','Impact','FloorImpact']:
    entry={}
    for label in ['Before','After']:
        path=root/'Saved/Profiling/CSV'/('BFGCombat'+label+'_'+phase+'.csv')
        if not path.exists():continue
        rows=list(csv.reader(path.open()));head=rows[0];data=[]
        for row in rows[1:]:
            try:data.append([0.]+[float(v) for v in row[1:]])
            except ValueError:pass
        entry[label]={'frames':len(data)}
        for name in metrics:
            if name not in head:continue
            i=head.index(name);values=sorted(row[i] for row in data if len(row)>i)
            if values:entry[label][name]={'mean':round(statistics.mean(values),3),'p95':round(values[int(.95*(len(values)-1))],3),'max':round(max(values),3)}
    report['phases'][phase]=entry
out=root/'Saved/WeaponCombat/Comparison.json';out.write_text(json.dumps(report,indent=2))
for phase,row in report['phases'].items():print(phase,{label:values.get('FrameTime') for label,values in row.items()})
