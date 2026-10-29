"""Upgrade the existing actor/Blueprint in place to the modern glass pyramid reference."""
import json
import runpy
import shutil
from pathlib import Path
import unreal

ROOT='/Game/Structures/Destructible/MayaPyramid'
MAT=ROOT+'/Materials'
lib=unreal.EditorAssetLibrary
out=Path(unreal.Paths.project_saved_dir())/'MayaPyramid/Architecture'
out.mkdir(parents=True,exist_ok=True)
content=Path(unreal.Paths.project_content_dir()).resolve()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
materials=runpy.run_path(str(Path(__file__).with_name('MayaArchitectureMaterials.py')))['create_palette']()
settings={'base_width':4200.,'tier_count':10,'tier_height':260.,'summit_width_ratio':.17,'piece_size':180.,
          'stair_width_ratio':.13,'side_staircases':True,
          'podium_height':420.,'podium_width_ratio':1.28,'slab_thickness':26.,'window_bay_width':220.,
          'animate_interior_lighting':True,'interior_brightness':1.,'lighting_cycle_seconds':8.}

def configure(actor):
    for key,value in {**settings,**materials}.items():actor.set_editor_property(key,value)

def protect(file):
    dest=out/'BeforeUpgrade'/file.relative_to(content)
    if file.exists() and not dest.exists():
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)

registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
bps=[]
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine','Blueprint'),True):
    parent=str(asset.get_tag_value('NativeParentClass') or asset.get_tag_value('ParentClass') or '')
    if 'ChunkedDestructibleMayaPyramid' not in parent:continue
    bp=asset.get_asset()
    if isinstance(unreal.get_default_object(bp.generated_class()),unreal.ChunkedDestructibleMayaPyramid):bps.append(bp)
assert bps,'The existing Maya Blueprint must retain its identity'
for bp in bps:
    protect(content/(bp.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset'))
    configure(unreal.get_default_object(bp.generated_class()))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert lib.save_loaded_asset(bp,False)

names=[b'ChunkedDestructibleMayaPyramid']+[bp.get_name().encode() for bp in bps]
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
    targets=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleMayaPyramid)]
    for actor in targets:
        transform,label,folder=actor.get_actor_transform(),actor.get_actor_label(),actor.get_folder_path()
        configure(actor);actor.rebuild_maya_pyramid()
        assert not actor.generation_warning,(label,actor.generation_warning)
        assert actor.get_actor_transform()==transform and actor.get_actor_label()==label and actor.get_folder_path()==folder
        records.append({'map':path,'label':label,'class':actor.get_class().get_path_name(),'pieces':actor.generated_piece_count,'height_cm':actor.generated_height})
    if targets:assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)

preview=ROOT+'/Maps/Lvl_MayaPyramidPreview'
world=unreal.EditorLoadingAndSavingUtils.load_map(preview)
pyramid=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleMayaPyramid))
def existing(label):return next((a for a in actors.get_all_level_actors() if a.get_actor_label()==label),None)
def cube(label,location,scale,material):
    actor=existing(label)
    if not actor:
        actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*location))
        actor.set_actor_label(label);actor.set_folder_path('Preview')
    actor.set_actor_location(unreal.Vector(*location),False,False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    component=actor.static_mesh_component
    component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    component.set_material(0,unreal.load_asset(MAT+'/MI_Maya_'+material))
    return actor
cube('Preview_Ground',(0,0,-65),(260,260,1.),'Courtyard')
# A quiet courtyard and shallow reflection pool supply architectural context, separate from the actor.
cube('Preview_ReflectionPool',(0,-3850,-8),(73,18,.16),'Pool')
for name,location,scale in [('Left',(-3740,-3850,-2),(1,20,.35)),('Right',(3740,-3850,-2),(1,20,.35)),
                           ('Front',(0,-4800,-2),(75,1,.35)),('Back',(0,-2900,-2),(75,1,.35))]:
    cube('Preview_PoolBorder_'+name,location,scale,'IvoryConcrete')
cube('Preview_CourtyardWallBack',(0,6500,180),(170,1.6,4.8),'IvoryConcrete')
cube('Preview_CourtyardWallLeft',(-7000,1000,180),(1.6,110,4.8),'IvoryConcrete')
for label,rotation,intensity,color in [
    ('Preview_Moon',unreal.Rotator(pitch=-43,yaw=115,roll=0),4.,unreal.LinearColor(1.,.93,.83,1)),
    ('Preview_Fill',unreal.Rotator(pitch=-20,yaw=-40,roll=0),.65,unreal.LinearColor(.68,.80,1.,1))]:
    light=existing(label)
    assert light
    light.set_actor_rotation(rotation,False)
    light.light_component.set_editor_property('intensity',intensity)
    light.light_component.set_light_color(color)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.SkyLight):
        actor.get_component_by_class(unreal.SkyLightComponent).set_editor_property('intensity',.55)
fog=existing('Preview_NightHaze').get_component_by_class(unreal.ExponentialHeightFogComponent)
for key,value in {'fog_density':.018,'fog_height_falloff':.12,'fog_inscattering_luminance':unreal.LinearColor(.075,.1,.16,1),
                  'fog_max_opacity':.85,'start_distance':6000.}.items():fog.set_editor_property(key,value)
post_actor=existing('Preview_NeonExposure')
post=post_actor.get_editor_property('settings')
for key,value in {'override_auto_exposure_bias':True,'auto_exposure_bias':.5,
                  'override_bloom_intensity':True,'bloom_intensity':.35,
                  'override_ambient_cubemap_intensity':True,'ambient_cubemap_intensity':.32,
                  'override_ambient_cubemap_tint':True,'ambient_cubemap_tint':unreal.LinearColor(.96,.93,.85,1),
                  'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':.8,
                  'override_vignette_intensity':True,'vignette_intensity':.15}.items():post.set_editor_property(key,value)
post_actor.set_editor_property('settings',post)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.PlayerStart):actor.set_actor_location(unreal.Vector(0,-3100,120),False,False)
location=unreal.Vector(7000,-9600,5900)
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(location,
    unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1250)))
actors.set_selected_level_actors([pyramid])
assert unreal.EditorLoadingAndSavingUtils.save_map(world,preview)
(out/'Setup.json').write_text(json.dumps({'settings':settings,'materials':{k:v.get_path_name() for k,v in materials.items()},
    'blueprints':[bp.get_path_name() for bp in bps],'instances':records},indent=2))
unreal.log('MAYA_ARCHITECTURE_SAVED '+json.dumps(records))
