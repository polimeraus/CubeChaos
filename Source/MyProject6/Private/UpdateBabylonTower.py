"""Persist the polygon reference preset in Babylon class defaults and existing instances.
Run in a closed-editor commandlet; backups and a verification manifest are kept under Saved.
"""
import json, shutil, runpy
from datetime import datetime
from pathlib import Path
import unreal

folder='/Game/Structures/Destructible/BabylonTower'
content=Path(unreal.Paths.project_content_dir()).resolve()
out=Path(unreal.Paths.project_saved_dir()).resolve()/'BabylonTower/PolygonUpgrade'
backup=out/'Backups'/datetime.now().strftime('%Y%m%d_%H%M%S')
backup.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
native=unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower)
keys=('side_count','base_width','base_depth','tier_count','tier_height','summit_height','top_scale',
      'wall_batter','wall_thickness','piece_size','piece_gap','open_facade_recesses','upper_windows',
      'windows_per_side','tier_window_counts','tier_window_widths','window_width','window_height',
      'window_sill_height','cornices','cornice_overhang','cornice_height','floor_thickness','summit_corbels',
      'entrance','entrance_count','entrance_width','entrance_height','entrance_frame','entrance_stairs',
      'stair_count','stair_tread_depth','stair_width','foundation_height','foundation_overhang')

def protect(file):
    if file.exists():
        dest=backup/file.relative_to(content);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)

def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]

def configure(a):
    a.modify()
    for key in keys:a.set_editor_property(key,native.get_editor_property(key))

# Give this tower a pale, grainy surface without changing the shared sandstone master.
master=runpy.run_path(str(Path(__file__).with_name('BabylonTowerMaterials.py')))['create_limestone_material'](folder)
for name,color in {'Stone':(.73,.70,.63),'Cornice':(.76,.73,.66),'Terrace':(.78,.75,.68),
                   'Portal':(.73,.70,.63),'Foundation':(.71,.68,.61),'Recess':(.60,.57,.51)}.items():
    path=folder+'/Materials/MI_Babylon'+name
    protect(content/(path.removeprefix('/Game/')+'.uasset'))
    m=unreal.load_asset(path)
    assert m,path
    unreal.MaterialEditingLibrary.set_material_instance_parent(m,master)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(m,'SandstoneColor',unreal.LinearColor(*color,1))
    assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)

registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.search_all_assets(True)
bps=[]
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine','Blueprint'),True):
    parent=str(asset.get_tag_value('NativeParentClass') or asset.get_tag_value('ParentClass') or '')
    if 'ChunkedDestructibleBabylonTower' not in parent:continue
    bp=asset.get_asset()
    if isinstance(unreal.get_default_object(bp.generated_class()),unreal.ChunkedDestructibleBabylonTower):bps.append(bp)
assert bps
for bp in bps:
    protect(content/(bp.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset'))
    configure(unreal.get_default_object(bp.generated_class()))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

names=[b'ChunkedDestructibleBabylonTower']+[b.get_name().encode() for b in bps]
records=[]
for file in sorted(content.rglob('*.umap')):
    external=content/'__ExternalActors__'/file.relative_to(content).with_suffix('')
    external_matches=[f for f in external.rglob('*.uasset') if any(n in f.read_bytes() for n in names)] if external.exists() else []
    if not external_matches and not any(n in file.read_bytes() for n in names):continue
    path='/Game/'+file.relative_to(content).with_suffix('').as_posix()
    protect(file)
    for package in external_matches:protect(package)
    world=unreal.EditorLoadingAndSavingUtils.load_map(path)
    assert world,path
    targets=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleBabylonTower)]
    for a in targets:
        transform,label,actor_folder=a.get_actor_transform(),a.get_actor_label(),a.get_folder_path()
        configure(a);a.rebuild_tower()
        assert not a.generation_warning,(label,a.generation_warning)
        assert a.get_actor_transform()==transform and a.get_actor_label()==label and a.get_folder_path()==actor_folder
        records.append({'map':path,'label':label,'class':a.get_class().get_path_name(),'pieces':a.generated_piece_count,
                        'height':a.generated_height,'openings':a.generated_opening_count,'transform':pose(a),'folder':str(actor_folder)})
    if targets:assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
assert records
(out/'SavedInstances.json').write_text(json.dumps({'keys':keys,'blueprints':[b.get_path_name() for b in bps],'instances':records},indent=2))
unreal.log('BABYLON_POLYGON_SAVED '+json.dumps(records))
