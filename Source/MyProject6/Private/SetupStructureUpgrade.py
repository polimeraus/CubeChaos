"""Create polygon tower assets, then persist traversal/rim/window changes in class defaults and placed actors."""
import json, shutil
from datetime import datetime
from pathlib import Path
import unreal

content=Path(unreal.Paths.project_content_dir())
out=Path(unreal.Paths.project_saved_dir())/'StructureUpgrade'
backup=out/'Backups'/datetime.now().strftime('%Y%m%d_%H%M%S')
backup.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools=unreal.AssetToolsHelpers.get_asset_tools()
folder='/Game/Structures/PolygonTower'
classes=(unreal.ChunkedDestructibleBabylonTower,unreal.ChunkedDestructiblePolygonTower,
         unreal.ChunkedDestructibleZiggurat,unreal.ChunkedDestructibleZigguratPyramit,
         unreal.ChunkedDestructibleConeStructure,unreal.QuadPanelConeStructure)
cones=classes[-2:]
def load(path):
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
def protect(path,suffix='.uasset'):
    f=content/(path.removeprefix('/Game/')+suffix)
    if f.exists():
        dest=backup/f.relative_to(content);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,dest)
def configure(a):
    a.modify()
    s=a.get_editor_property('traversal')
    s.set_editor_property('floor_openings',True);s.set_editor_property('ground_launch_pad',True)
    a.set_editor_property('traversal',s)
    if isinstance(a,unreal.ChunkedDestructibleBabylonTower):a.set_editor_property('open_facade_recesses',True)
    if isinstance(a,cones):a.set_editor_property('spiral_outer_rim',True)
def rebuild(a):
    if isinstance(a,cones):a.rebuild_cone_structure(False)
    elif isinstance(a,unreal.ChunkedDestructibleZiggurat):a.rebuild_ziggurat()
    elif isinstance(a,unreal.ChunkedDestructibleZigguratPyramit):a.rebuild_ziggurat_pyramit()
    else:a.rebuild_tower()

palette={}
for name,color in {'Stone':(.55,.52,.46),'Cornice':(.68,.65,.58),'Floor':(.70,.67,.60)}.items():
    path=folder+'/MI_Polygon'+name
    m=load(path)
    if not m:
        m=tools.create_asset('MI_Polygon'+name,folder,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(m,load('/Game/Structures/BabylonTower/M_BabylonSandstone'))
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(m,'SandstoneColor',unreal.LinearColor(*color,1))
        assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
    palette[name.lower()+'_material']=m
bp=load(folder+'/BP_ChunkedDestructiblePolygonTower')
if not bp:
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ChunkedDestructiblePolygonTower)
    bp=tools.create_asset('BP_ChunkedDestructiblePolygonTower',folder,unreal.Blueprint,factory)
    defaults=unreal.get_default_object(bp.generated_class())
    for k,v in palette.items():defaults.set_editor_property(k,v)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

# Inspect all Blueprint classes through the registry, including derived user variants.
registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.search_all_assets(True)
bps=[]
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine','Blueprint'),True):
    parent=str(asset.get_tag_value('NativeParentClass') or asset.get_tag_value('ParentClass') or '')
    if not any(c.static_class().get_name() in parent for c in classes):continue
    candidate=asset.get_asset();cdo=unreal.get_default_object(candidate.generated_class())
    if isinstance(cdo,classes):bps.append(candidate)
if bp not in bps:bps.append(bp)
for candidate in bps:
    protect(candidate.get_path_name().split('.')[0])
    configure(unreal.get_default_object(candidate.generated_class()))
    unreal.BlueprintEditorLibrary.compile_blueprint(candidate)
    assert unreal.EditorAssetLibrary.save_loaded_asset(candidate,False)

preview=folder+'/Lvl_PolygonTowerPreview'
if not unreal.EditorAssetLibrary.does_asset_exist(preview):
    world=unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/BabylonTower/Lvl_BabylonTowerPreview')
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.ChunkedDestructibleBabylonTower):actors.destroy_actor(a)
    tower=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
    tower.set_actor_label('ChunkedDestructiblePolygonTower');tower.set_folder_path('Polygon Tower')
    cone=actors.spawn_actor_from_class(unreal.QuadPanelConeStructure,unreal.Vector(-4500,0,0))
    cone.set_actor_label('SpiralCone_OuterRim');cone.set_folder_path('Spiral Cone')
    for k,v in {'platform_style':unreal.ConeStructurePlatformStyle.RISING_SPIRAL,'platform_spiral_turns':4.,
                'lowest_platform_height_blocks':1,'spiral_end_height_blocks':30,'platform_overhang_blocks':3.,
                'spiral_rim_height':35.,'spiral_rim_width':25.,'cone_material':palette['stone_material'],
                'platform_material':palette['floor_material'],'react_to_music':False}.items():cone.set_editor_property(k,v)
    cone.configure_cone_structure(34,12.,0.,.6,unreal.Vector(100,100,100),unreal.Vector())
    location=unreal.Vector(4200,-9300,2800)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(location,
        unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1850)))
    actors.set_selected_level_actors([tower])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,preview)

names=[c.static_class().get_name().encode() for c in classes]+[b.get_name().encode() for b in bps]
records=[]
for source in sorted(content.rglob('*.umap')):
    data=source.read_bytes()
    if not any(n in data for n in names) and source.name!='Lvl_FirstPerson.umap':continue
    external=content/'__ExternalActors__'/source.relative_to(content).with_suffix('')
    if external.exists():
        for package in external.rglob('*.uasset'):
            if any(n in package.read_bytes() for n in names):
                dest=backup/package.relative_to(content);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(package,dest)
    path='/Game/'+source.relative_to(content).with_suffix('').as_posix();protect(path,'.umap')
    world=unreal.EditorLoadingAndSavingUtils.load_map(path)
    targets=[a for a in actors.get_all_level_actors() if isinstance(a,classes)]
    for a in targets:
        transform,label,actor_folder=a.get_actor_transform(),a.get_actor_label(),a.get_folder_path()
        configure(a);rebuild(a)
        assert a.get_actor_transform()==transform and a.get_actor_label()==label and a.get_folder_path()==actor_folder
        pad=a.get_editor_property('ground_launch_pad').get_editor_property('child_actor')
        assert isinstance(pad,unreal.LaunchPad),label
        row={'map':path,'label':label,'class':a.get_class().get_path_name(),'opening_width':a.effective_opening_width}
        if isinstance(a,cones):row['pieces']=a.get_cone_structure_metrics().generated_block_count
        else:
            assert not a.generation_warning,(label,a.generation_warning)
            row.update(pieces=a.generated_piece_count,height=a.generated_height)
        records.append(row)
    if targets:assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
(out/'SavedInstances.json').write_text(json.dumps(records,indent=2))
(out/'Blueprints.json').write_text(json.dumps([b.get_path_name() for b in bps],indent=2))
unreal.log('STRUCTURE_UPGRADE_SAVED '+json.dumps(records))
