"""Persist the revised reference silhouette on the native preset, Blueprint and saved instances."""
import json, shutil, runpy
from pathlib import Path
import unreal

content=Path(unreal.Paths.project_content_dir()).resolve()
out=Path(unreal.Paths.project_saved_dir()).resolve()/'SpiralTower/ClearReference'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
native=unreal.get_default_object(unreal.ChunkedDestructibleSpiralTower)
keys=('base_radius','top_radius','turns','rise_per_turn','start_angle','clockwise','walkway_width','steps_per_turn',
      'slab_thickness','wall_height','wall_thickness','wall_inset','foundation','foundation_height','foundation_margin',
      'fit_stairs_between_walls','summit_wall_extension','top_rise_scale','reverse_summit_crescent')

def protect(file):
    if file.exists():
        dest=out/'Before/Content'/file.relative_to(content)
        if not dest.exists():
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(file,dest)

def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]

def configure(a):
    a.modify()
    for key in keys:a.set_editor_property(key,native.get_editor_property(key))

for file in (content/'Structures/Destructible/SpiralTower/Materials').glob('*.uasset'):protect(file)
ground=runpy.run_path(str(Path(__file__).with_name('SpiralTowerLimestone.py')))['update_materials']()

registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
bps=[]
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine','Blueprint'),True):
    parent=str(asset.get_tag_value('NativeParentClass') or asset.get_tag_value('ParentClass') or '')
    if 'ChunkedDestructibleSpiralTower' not in parent:continue
    bp=asset.get_asset()
    if isinstance(unreal.get_default_object(bp.generated_class()),unreal.ChunkedDestructibleSpiralTower):bps.append(bp)
assert bps
for bp in bps:
    protect(content/(bp.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset'))
    configure(unreal.get_default_object(bp.generated_class()))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

names=[b'ChunkedDestructibleSpiralTower']+[b.get_name().encode() for b in bps]
records=[]
for file in sorted(content.rglob('*.umap')):
    external=content/'__ExternalActors__'/file.relative_to(content).with_suffix('')
    matches=[f for f in external.rglob('*.uasset') if any(n in f.read_bytes() for n in names)] if external.exists() else []
    if not matches and not any(n in file.read_bytes() for n in names):continue
    path='/Game/'+file.relative_to(content).with_suffix('').as_posix()
    protect(file)
    for package in matches:protect(package)
    world=unreal.EditorLoadingAndSavingUtils.load_map(path)
    assert world,path
    targets=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleSpiralTower)]
    if path.endswith('/Lvl_SpiralTowerPreview'):
        for a in actors.get_all_level_actors():
            if a.get_actor_label()=='Preview_Ground':a.static_mesh_component.set_material(0,ground)
            if a.get_actor_label()=='Preview_Key':
                a.set_actor_rotation(unreal.Rotator(pitch=-50,yaw=-35,roll=0),False)
                a.light_component.set_editor_property('intensity',12.)
                a.light_component.set_editor_property('light_source_angle',7.)
            if a.get_actor_label()=='Preview_Fill':
                a.light_component.set_editor_property('intensity',6.)
            if isinstance(a,unreal.SkyLight):
                a.get_component_by_class(unreal.SkyLightComponent).set_editor_property('intensity',1.3)
        camera=unreal.Vector(0,-10000,8335)
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
            camera,unreal.MathLibrary.find_look_at_rotation(camera,unreal.Vector(0,0,1735)))
    for a in targets:
        transform,label,folder=a.get_actor_transform(),a.get_actor_label(),a.get_folder_path()
        configure(a)
        a.rebuild_tower()
        assert not a.generation_warning,(label,a.generation_warning)
        assert a.get_actor_transform()==transform and a.get_actor_label()==label and a.get_folder_path()==folder
        records.append(dict(map=path,label=label,cls=a.get_class().get_path_name(),transform=pose(a),folder=str(folder),
                            pieces=a.generated_piece_count,height=a.generated_height))
    if targets:
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
        assert unreal.EditorLoadingAndSavingUtils.save_packages([a.get_package() for a in targets],False)
assert records
(out/'SavedInstances.json').write_text(json.dumps(dict(keys=keys,blueprints=[b.get_path_name() for b in bps],instances=records),indent=2))
unreal.log('SPIRAL_REFERENCE_SAVED '+json.dumps(records))
