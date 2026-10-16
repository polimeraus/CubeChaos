"""Load saved packages in a fresh editor process and verify class/instance agreement."""
from pathlib import Path
import unreal,json
out=Path(unreal.Paths.project_saved_dir())/'RailTowerUpgrade'
data=json.loads((out/'TowerSettings.json').read_text())
settings=data['settings']
bp=unreal.load_asset(data['class']);assert bp
for obj in [unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower),unreal.get_default_object(bp.generated_class())]:
    for key,value in settings.items():assert obj.get_editor_property(key)==value
    assert obj.entrance_height==200
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for map_path in sorted(set(r['map'] for r in data['instances'])):
    assert level.load_level(map_path)
    for r in [r for r in data['instances'] if r['map']==map_path]:
        a=next(a for a in actors.get_all_level_actors() if a.get_actor_label()==r['label'])
        for key,value in settings.items():assert a.get_editor_property(key)==value
        p,rot,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
        assert [p.x,p.y,p.z,rot.pitch,rot.yaw,rot.roll,s.x,s.y,s.z]==r['pose']
        assert str(a.get_folder_path())==r['folder'] and a.entrance_height==200
        assert not a.generation_warning and a.generated_piece_count>0
        new=actors.spawn_actor_from_class(a.get_class(),unreal.Vector(25000,0,0))
        for key,value in settings.items():assert new.get_editor_property(key)==value
        assert not new.generation_warning and new.generated_piece_count>0
        actors.destroy_actor(new)
(out/'Persistence.json').write_text(json.dumps({'fresh_process_verified':True,**data},indent=2))
unreal.log('TOWER_OPENINGS_PERSISTENCE_OK')
