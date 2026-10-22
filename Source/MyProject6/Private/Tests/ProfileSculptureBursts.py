"""Repeatable CPU cost of the BFG's sculpture damage path; never saves levels."""
import json, math, time, statistics
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'BombRetro'
out.mkdir(parents=True,exist_ok=True)
label='After' if '-RetroAfter' in unreal.SystemLibrary.get_command_line() else 'Before'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
bp=unreal.load_class(None,'/Game/Structures/Destructible/RadialSculpture/BP_DestructibleRadialSculpture.BP_DestructibleRadialSculpture_C')
sculpt=actors.spawn_actor_from_class(bp,unreal.Vector())
sculpt.set_actor_scale3d(unreal.Vector(5.75,5.75,5.75))
sculpt.set_editor_property('use_destruction_ripple',False)
sculpt.set_editor_property('play_destroy_tick_sound',False)
sculpt.set_editor_property('flash_duration',0.)
rows=[]
for repeat in range(3):
    sculpt.rebuild_sculpture()
    for i in range(32):
        a=i*2.399963
        local=unreal.Vector(math.cos(a)*(170+i%3*55),math.sin(a)*(170+i%3*55),70+i%5*65)
        point=unreal.MathLibrary.transform_location(sculpt.get_actor_transform(),local)
        start=time.perf_counter()
        count=sculpt.apply_explosion(point,450.,300.)
        elapsed=(time.perf_counter()-start)*1000
        rows.append({'repeat':repeat,'shot':i,'ms':elapsed,'affected':count,'remaining':sculpt.remaining_piece_count})
times=sorted(r['ms'] for r in rows)
report={'label':label,'mean_ms':statistics.mean(times),'p95_ms':times[int(len(times)*.95)],'max_ms':max(times),'samples':rows}
(out/('Sculpture'+label+'.json')).write_text(json.dumps(report,indent=2))
unreal.log('SCULPTURE_PROFILE '+str({k:v for k,v in report.items() if k!='samples'}))
