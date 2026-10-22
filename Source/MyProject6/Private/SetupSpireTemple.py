"""Create the reusable spire temple Blueprint and an isolated, saved reference preview."""
from pathlib import Path
import json
import unreal

folder = '/Game/Structures/Destructible/SpireTemple'
out = Path(unreal.Paths.project_saved_dir()) / 'SpireTemple'
out.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
edit = unreal.MaterialEditingLibrary
master_path = folder + '/Materials/M_SpireLimestone'
master = unreal.load_asset(master_path) if unreal.EditorAssetLibrary.does_asset_exist(master_path) else None
if not master:
    master = assets.create_asset('M_SpireLimestone', folder+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
    tint = edit.create_material_expression(master, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name', 'StoneColor')
    tint.set_editor_property('default_value', unreal.LinearColor(.52,.48,.40,1))
    position = edit.create_material_expression(master, unreal.MaterialExpressionWorldPosition)
    color = edit.create_material_expression(master, unreal.MaterialExpressionCustom)
    pins=[]
    for name in ('Position','Tint'):
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    color.set_editor_property('inputs',pins)
    color.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    color.set_editor_property('code','''
struct StoneNoise {
float Hash(float3 p) {p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z);}
float Value(float3 p) {
 float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(lerp(Hash(i),Hash(i+float3(1,0,0)),f.x),lerp(Hash(i+float3(0,1,0)),Hash(i+float3(1,1,0)),f.x),f.y),
 lerp(lerp(Hash(i+float3(0,0,1)),Hash(i+float3(1,0,1)),f.x),lerp(Hash(i+float3(0,1,1)),Hash(i+float3(1,1,1)),f.x),f.y),f.z);
}}; StoneNoise noise;
return Tint*(.94+.045*noise.Value(Position*.018)+.015*noise.Value(Position*.12));
''')
    rough=edit.create_material_expression(master,unreal.MaterialExpressionScalarParameter)
    rough.set_editor_property('parameter_name','Roughness');rough.set_editor_property('default_value',.9)
    edit.connect_material_expressions(position,'',color,'Position');edit.connect_material_expressions(tint,'RGB',color,'Tint')
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master,False)
palette={}
for name,key,rgb in [('Stone','stone_material',(.55,.50,.41)),('Trim','trim_material',(.64,.59,.49)),
                     ('Cone','spire_material',(.58,.53,.44)),('Floor','floor_material',(.51,.47,.40))]:
    path=folder+'/Materials/MI_Spire'+name
    material=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not material:
        material=assets.create_asset('MI_Spire'+name,folder+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(material,master)
        edit.set_material_instance_vector_parameter_value(material,'StoneColor',unreal.LinearColor(*rgb,1))
        assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)
    palette[key]=material
bp_path = folder + '/BP_ChunkedDestructibleSpireTemple'
bp = unreal.load_asset(bp_path) if unreal.EditorAssetLibrary.does_asset_exist(bp_path) else None
if not bp:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ChunkedDestructibleSpireTemple)
    bp = assets.create_asset('BP_ChunkedDestructibleSpireTemple', folder, unreal.Blueprint, factory)
for key,value in palette.items():
    unreal.get_default_object(bp.generated_class()).set_editor_property(key,value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

stage_path = folder + '/Materials/M_SpirePreviewGround'
stage = unreal.load_asset(stage_path) if unreal.EditorAssetLibrary.does_asset_exist(stage_path) else None
if not stage:
    stage = assets.create_asset('M_SpirePreviewGround', folder + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    color = edit.create_material_expression(stage, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(.58, .58, .58, 1))
    rough = edit.create_material_expression(stage, unreal.MaterialExpressionConstant)
    rough.set_editor_property('r', .95)
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(stage)
    assert unreal.EditorAssetLibrary.save_loaded_asset(stage, False)

map_path = folder + '/Maps/Lvl_SpireTemplePreview'
if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    temple = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
    temple.set_actor_label('ChunkedDestructibleSpireTemple')
    temple.set_folder_path('Spire Temple')
    for label, rotation, intensity in [('Key', unreal.Rotator(pitch=-38, yaw=65, roll=0), 5.),
                                        ('Fill', unreal.Rotator(pitch=-25, yaw=155, roll=0), 2.)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,4000), rotation)
        light.set_actor_label('Preview_' + label)
        light.set_folder_path('Preview')
        light.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property('intensity', intensity)
        light.light_component.set_editor_property('cast_shadows', label == 'Key')
    ground = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-26))
    ground.set_actor_label('Preview_Ground')
    ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0, stage)
    ground.set_actor_scale3d(unreal.Vector(10000,10000,.5))
    ambient = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    ambient.set_actor_label('Preview_Ambient')
    ambient.set_folder_path('Preview')
    ambient.set_editor_property('unbound', True)
    post = ambient.get_editor_property('settings')
    for key, value in {'ambient_cubemap': unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
                       'override_ambient_cubemap_intensity': True, 'ambient_cubemap_intensity': .55,
                       'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                       'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                       'override_auto_exposure_bias': True, 'auto_exposure_bias': .1,
                       'override_ambient_occlusion_intensity': True, 'ambient_occlusion_intensity': 1.0}.items():
        post.set_editor_property(key, value)
    ambient.set_editor_property('settings', post)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,-2100,110), unreal.Rotator(pitch=0,yaw=90,roll=0))
    start.set_folder_path('Preview')
    location = unreal.Vector(3900,-10500,4400)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location, unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0,0,1400)))
    actors.set_selected_level_actors([temple])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
else:
    unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    temple = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleSpireTemple))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for key,value in palette.items():temple.set_editor_property(key,value)
temple.rebuild_temple()
# Studio lighting belongs only to this preview map. Bias suppresses shadow-map acne
# on the many thin tapered fragments, without changing their geometry or collision.
for actor in actors.get_all_level_actors():
    if actor.get_actor_label()=='Preview_Key':
        actor.set_actor_rotation(unreal.Rotator(pitch=-38,yaw=65,roll=0),False)
        actor.light_component.set_editor_property('intensity',5.)
        actor.light_component.set_editor_property('shadow_bias',1.)
        actor.light_component.set_editor_property('shadow_slope_bias',1.)
        actor.light_component.set_editor_property('light_source_angle',3.)
    elif actor.get_actor_label()=='Preview_Fill':
        actor.light_component.set_editor_property('intensity',2.)
    elif actor.get_actor_label()=='Preview_Ambient':
        post=actor.get_editor_property('settings')
        post.set_editor_property('ambient_cubemap_intensity',.8)
        actor.set_editor_property('settings',post)
edit.get_material_property_input_node(stage,unreal.MaterialProperty.MP_BASE_COLOR).set_editor_property('constant',unreal.LinearColor(.58,.58,.58,1))
edit.recompile_material(stage)
assert unreal.EditorAssetLibrary.save_loaded_asset(stage,False)
location=unreal.Vector(3900,-10500,4400)
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(0,0,1450)))
assert unreal.EditorLoadingAndSavingUtils.save_map(world,map_path)
assert temple.generated_piece_count > 1000 and not temple.generation_warning
(out/'Setup.json').write_text(json.dumps({'blueprint': bp_path, 'map': map_path,
    'pieces': temple.generated_piece_count, 'enclosure_pieces': temple.generated_enclosure_pieces,
    'spire_pieces': temple.generated_spire_pieces, 'openings': temple.generated_opening_count,
    'height_cm': temple.generated_height}, indent=2))
unreal.log('SPIRE_TEMPLE_SETUP_OK')
