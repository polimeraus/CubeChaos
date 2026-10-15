"""Load the saved polygon preset in a fresh process and compare placed/BP/native/new actors."""
import json
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir())/'BabylonTower/PolygonUpgrade'
manifest=json.loads((out/'SavedInstances.json').read_text())
native=unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]
def check(a):
    for k in manifest['keys']:
        assert a.get_editor_property(k)==native.get_editor_property(k),(a.get_name(),k)
    for k in ('stone_material','cornice_material','terrace_material','portal_material','foundation_material','recess_material'):
        material=a.get_editor_property(k)
        assert material.get_editor_property('parent').get_name()=='M_BabylonLimestone',(a.get_name(),k)
for path in manifest['blueprints']:
    bp=unreal.load_asset(path);check(unreal.get_default_object(bp.generated_class()))
for path in sorted(set(r['map'] for r in manifest['instances'])):
    assert unreal.EditorLoadingAndSavingUtils.load_map(path)
    for row in [r for r in manifest['instances'] if r['map']==path]:
        a=next(a for a in actors.get_all_level_actors() if a.get_actor_label()==row['label'] and a.get_class().get_path_name()==row['class'])
        check(a)
        assert pose(a)==row['transform'] and str(a.get_folder_path())==row['folder']
        assert a.generated_piece_count==row['pieces'] and a.generated_opening_count==row['openings'] and not a.generation_warning
        fresh=actors.spawn_actor_from_class(a.get_class(),unreal.Vector(20000,0,0))
        check(fresh)
        assert fresh.generated_piece_count==a.generated_piece_count and fresh.generated_opening_count==a.generated_opening_count
        fresh.set_editor_property('side_count',7);fresh.set_editor_property('tier_count',4)
        fresh.set_editor_property('entrance_count',1);fresh.set_editor_property('windows_per_side',2);fresh.rebuild_tower()
        assert fresh.generated_opening_count==43 and not fresh.generation_warning
        actors.destroy_actor(fresh)
result={'fresh_process_persistence':True,'native_blueprint_placed_and_new_spawns_match':True,
        'saved_instances':manifest['instances'],'heptagon_parameter_check':True}
(out/'Verification.json').write_text(json.dumps(result,indent=2))
unreal.log('BABYLON_POLYGON_VERIFIED '+json.dumps(result))
