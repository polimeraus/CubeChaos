"""Create calibrated reusable Blueprints and three isolated, playable city example maps.
Run in UnrealEditor-Cmd with -ExecutePythonScript=... -NullRHI -unattended.
Only the new /Game/Procedural/CityGenerator assets/maps are written.
"""
import json
from pathlib import Path
import unreal

ROOT = '/Game/Procedural/CityGenerator'
out = Path(unreal.Paths.project_saved_dir())/'ProceduralCity'
out.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

def blueprint(name, parent):
    path = ROOT+'/'+name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    return assets.create_asset(name, ROOT, unreal.Blueprint, factory)

link_bp = blueprint('BP_ParametricCityLink', unreal.ParametricCityLink)
unreal.BlueprintEditorLibrary.compile_blueprint(link_bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(link_bp, False)
bp = blueprint('BP_ProceduralCityGenerator', unreal.ProceduralCityGenerator)

unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
calibrator = actors.spawn_actor_from_class(unreal.ProceduralCityGenerator, unreal.Vector())
assert calibrator.load_rules_from_file(), calibrator.last_error
paths = {
    'RoundTemple': '/Game/Structures/Destructible/RoundTemple/BP_ChunkedDestructibleRoundTemple',
    'House': '/Game/Structures/Destructible/House/BP_ChunkedDestructibleHouse',
    'PolygonTower': '/Game/Structures/Destructible/PolygonTower/BP_ChunkedDestructiblePolygonTower',
    'FantasyHouse': '/Game/Structures/DynamicMesh/FantasyHouse/BP_ParametricFantasyHouse',
    'SpireTemple': '/Game/Structures/Destructible/SpireTemple/BP_ChunkedDestructibleSpireTemple',
}
ranges = {'RoundTemple':(.4,.65,1.2), 'House':(.45,.8,2.), 'PolygonTower':(.2,.35,.8),
          'FantasyHouse':(.65,1.,2.), 'SpireTemple':(.25,.4,.65)}
catalog = list(calibrator.get_editor_property('building_catalog'))
for entry in catalog:
    key = str(entry.get_editor_property('id'))
    cls = unreal.EditorAssetLibrary.load_blueprint_class(paths[key])
    assert cls, paths[key]
    entry.set_editor_property('actor_class', cls)
    lo, hi, weight = ranges[key]
    for prop, value in [('min_scale',lo),('max_scale',hi),('weight',weight)]:
        entry.set_editor_property(prop, value)
calibrator.set_editor_property('building_catalog', catalog)
assert calibrator.calibrate_catalog_footprints(), calibrator.last_error
catalog = list(calibrator.get_editor_property('building_catalog'))
catalog_report = []
for entry in catalog:
    offset = entry.get_editor_property('pivot_offset')
    catalog_report.append({
        'id':str(entry.get_editor_property('id')), 'class':entry.get_editor_property('actor_class').get_path_name(),
        'footprint_radius':entry.get_editor_property('footprint_radius'),
        'pivot_offset':[offset.x,offset.y,offset.z], 'estimated_cells':entry.get_editor_property('estimated_cells'),
        'min_scale':entry.get_editor_property('min_scale'), 'max_scale':entry.get_editor_property('max_scale')})
(out/'Catalog.json').write_text(json.dumps(catalog_report,indent=2))
cdo = unreal.get_default_object(bp.generated_class())
for prop,value in [('building_catalog',catalog),('rules',calibrator.get_editor_property('rules')),
                   ('link_class',link_bp.generated_class()),('seed',20261028),
                   ('generate_on_begin_play',True),('new_seed_on_begin_play',True),('auto_rebuild_in_editor',False)]:
    cdo.set_editor_property(prop,value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
actors.destroy_actor(calibrator)

def stage(world):
    for label,yaw,pitch,intensity in [('Key',55,-48,4.5),('Fill',-95,-35,1.6)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,10000),unreal.Rotator(pitch=pitch,yaw=yaw))
        light.set_actor_label('CityPreview_'+label);light.set_folder_path('Preview')
        comp = light.light_component
        for key,value in [('mobility',unreal.ComponentMobility.MOVABLE),('intensity',intensity),
                           ('cast_shadows',label=='Key'),('shadow_bias',.8),('light_source_angle',4.)]:
            comp.set_editor_property(key,value)
    ambient = actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector())
    ambient.set_actor_label('CityPreview_Exposure');ambient.set_folder_path('Preview');ambient.set_editor_property('unbound',True)
    post = ambient.get_editor_property('settings')
    for key,value in {'ambient_cubemap':unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
                      'override_ambient_cubemap_intensity':True,'ambient_cubemap_intensity':.65,
                      'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                      'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
                      'override_auto_exposure_bias':True,'auto_exposure_bias':1.5,
                      'override_bloom_intensity':True,'bloom_intensity':.25}.items():
        post.set_editor_property(key,value)
    ambient.set_editor_property('settings',post)
    sky = actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,10000))
    sky.set_actor_label('CityPreview_Sky');sky.set_folder_path('Preview')
    comp=sky.get_component_by_class(unreal.SkyLightComponent)
    comp.set_editor_property('mobility',unreal.ComponentMobility.MOVABLE)
    comp.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    comp.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    comp.set_editor_property('intensity',1.)

results=[]
for name,mode,seed in [('Radial',unreal.CityLayout.RADIAL,20261028),('Grid',unreal.CityLayout.GRID,89173),('Organic',unreal.CityLayout.ORGANIC,411701)]:
    map_path=ROOT+'/Maps/Lvl_City_'+name
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        world=unreal.EditorLoadingAndSavingUtils.load_map(map_path)
        city=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ProceduralCityGenerator))
    else:
        world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        city=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector())
        city.set_actor_label('ProceduralCity_'+name)
        stage(world)
    city.set_editor_property('building_catalog',catalog)
    city.set_editor_property('link_class',link_bp.generated_class())
    city.set_editor_property('create_foundation',name!='Organic')
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.PostProcessVolume) and a.get_actor_label()=='CityPreview_Exposure':
            settings=a.get_editor_property('settings')
            settings.set_editor_property('auto_exposure_bias',1.5)
            a.set_editor_property('settings',settings)
    city.apply_layout_preset(mode)
    if name=='Radial':assert city.load_rules_from_file(),city.last_error
    assert city.generate_with_seed(seed),city.last_error
    plan=city.generated_plan
    bounds=city.get_actor_bounds(False,True)
    # The generator itself has no renderable primitive: frame all district centres explicitly.
    districts=list(plan.get_editor_property('districts'))
    xs=[d.center.x for d in districts];ys=[d.center.y for d in districts]
    center=unreal.Vector((min(xs)+max(xs))*.5,(min(ys)+max(ys))*.5,650)
    extent=max(max(xs)-min(xs),max(ys)-min(ys))+8000
    location=center+unreal.Vector(extent*.75,-extent*.95,extent*.95)
    editor.set_level_viewport_camera_info(location,unreal.MathLibrary.find_look_at_rotation(location,center))
    actors.set_selected_level_actors([city])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,map_path)
    results.append({'map':map_path,'seed':seed,'signature':plan.signature,'districts':len(plan.districts),
                    'connections':len(plan.connections),'buildings':len(plan.buildings),'actors':city.generated_actor_count,
                    'estimated_cells':plan.estimated_cells,'notes':plan.notes})
    unreal.log('CITY_MAP_SAVED '+str(results[-1]))
(out/'Setup.json').write_text(json.dumps({'blueprint':bp.get_path_name(),'link_blueprint':link_bp.get_path_name(),
                                        'catalog':catalog_report,'maps':results},indent=2))
unreal.log('PROCEDURAL_CITY_SETUP_OK')
