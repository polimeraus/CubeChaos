"""Identical headless CPU workload before/after; excludes GPU and world simulation."""
from pathlib import Path
import json, statistics, time, unreal
out=Path(unreal.Paths.project_saved_dir()).resolve()/'NeonBlast';out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
shots=[actors.spawn_actor_from_class(unreal.BFGProjectile,unreal.Vector(500+i*100,0,200)) for i in range(32)]
view=unreal.Vector(0,-100,180)
results={}
for impact in [False,True]:
    times=[]
    for frame in range(140):
        start=time.perf_counter()
        for i,shot in enumerate(shots):shot.preview_effect((frame%30)/60 if impact else frame/60+i*.07,impact,view)
        if frame>=20:times.append((time.perf_counter()-start)*1000)
    results['impact' if impact else 'flight']={'median_ms_32_projectiles':statistics.median(times),
        'p95_ms_32_projectiles':sorted(times)[int(len(times)*.95)],'mean_us_per_projectile':statistics.mean(times)*1000/32}
label='After' if '-BFGPerfAfter' in unreal.SystemLibrary.get_command_line() else 'Before'
(out/('BFG'+label+'.json')).write_text(json.dumps(results,indent=2))
unreal.log('BFG_VISUAL_BENCHMARK_'+label.upper()+' '+json.dumps(results))
