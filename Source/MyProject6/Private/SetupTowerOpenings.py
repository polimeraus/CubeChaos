"""Reconcile only doorway/window dimensions on the reusable class and saved tower instances."""
from pathlib import Path
from datetime import datetime
import unreal,json,shutil
root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/RailTowerUpgrade';out.mkdir(parents=True,exist_ok=True)
backup=out/'Backups'/datetime.now().strftime('%Y%m%d_%H%M%S')
settings={'entrance_width':110.,'window_width':110.,'window_height':135.}
path='/Game/Structures/Destructible/BabylonTower/BP_ChunkedDestructibleBabylonTower'
def protect(asset,extension='.uasset'):
    file=root/'Content'/(asset.removeprefix('/Game/')+extension)
    dest=backup/file.relative_to(root/'Content');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)
def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]
native=unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower)
assert all(native.get_editor_property(k)==v for k,v in settings.items())
protect(path);bp=unreal.load_asset(path);assert bp
cdo=unreal.get_default_object(bp.generated_class())
for k,v in settings.items():cdo.set_editor_property(k,v)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
records=[]
for map_path in ['/Game/FirstPerson/Lvl_FirstPerson','/Game/Structures/Destructible/BabylonTower/Maps/Lvl_BabylonTowerPreview']:
    protect(map_path,'.umap');assert level.load_level(map_path)
    for a in actors.get_all_level_actors():
        if not isinstance(a,unreal.ChunkedDestructibleBabylonTower):continue
        original=pose(a);label=a.get_actor_label();folder=str(a.get_folder_path())
        # Inspected instances use the global window size, with no tier-width overrides.
        assert not any(w>0 for w in a.tier_window_widths),'Unexpected tier width override: '+label
        for k,v in settings.items():a.set_editor_property(k,v)
        a.rebuild_tower()
        assert not a.generation_warning and a.generated_piece_count>0
        assert pose(a)==original and a.get_actor_label()==label and str(a.get_folder_path())==folder
        records.append({'map':map_path,'label':label,'pose':original,'folder':folder,'settings':settings})
    assert level.save_current_level()
(out/'TowerSettings.json').write_text(json.dumps({'class':path,'settings':settings,'instances':records},indent=2))
unreal.log('TOWER_OPENINGS_SAVED')
