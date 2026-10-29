"""Save the reference globe defaults, pale plywood faces and dark cut edges, plus the playable preview."""
import json
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/Globe'
out = Path(unreal.Paths.project_saved_dir()) / 'Globe'
out.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
edit = unreal.MaterialEditingLibrary

def asset(name, directory, cls, factory):
    path = directory + '/' + name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name, directory, cls, factory)

master_path = folder + '/Materials/M_GlobeMDF'
master = unreal.load_asset(master_path) if unreal.EditorAssetLibrary.does_asset_exist(master_path) else None
if not master:
    master = assets.create_asset('M_GlobeMDF', folder + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    tint = edit.create_material_expression(master, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name', 'PanelColor')
    tint.set_editor_property('default_value', unreal.LinearColor(.34, .18, .085, 1))
    position = edit.create_material_expression(master, unreal.MaterialExpressionWorldPosition)
    grain = edit.create_material_expression(master, unreal.MaterialExpressionCustom)
    pins = []
    for name in ('Position', 'Tint'):
        pin = unreal.CustomInput(); pin.set_editor_property('input_name', name); pins.append(pin)
    grain.set_editor_property('inputs', pins)
    grain.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    grain.set_editor_property('code', '''
float3 p = floor(Position * 1.7);
float noise = frac(sin(dot(p, float3(12.9898,78.233,37.719))) * 43758.5453);
return Tint * (0.91 + 0.18 * noise);
''')
    edit.connect_material_expressions(position, '', grain, 'Position')
    edit.connect_material_expressions(tint, 'RGB', grain, 'Tint')
    edit.connect_material_property(grain, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = edit.create_material_expression(master, unreal.MaterialExpressionConstant)
    rough.set_editor_property('r', .88)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master, False)

palette = {}
# Subtle fiber variation lets the pale faces and dark laser-cut edges carry the reference appearance.
grain = edit.get_material_property_input_node(master, unreal.MaterialProperty.MP_BASE_COLOR)
grain.set_editor_property('code', '''
float3 p = floor(Position * 1.7);
float noise = frac(sin(dot(p, float3(12.9898,78.233,37.719))) * 43758.5453);
return Tint * (0.97 + 0.06 * noise);
''')
edit.recompile_material(master)
assert unreal.EditorAssetLibrary.save_loaded_asset(master, False)
for name, key, color in [('Panel', 'panel_material', (.65,.55,.34)), ('Edge', 'edge_material', (.12,.065,.025))]:
    material = asset('MI_Globe' + name, folder + '/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(material, master)
    edit.set_material_instance_vector_parameter_value(material, 'PanelColor', unreal.LinearColor(*color, 1))
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    palette[key] = material

factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.ChunkedDestructibleGlobe)
bp = asset('BP_ChunkedDestructibleGlobe', folder, unreal.Blueprint, factory)
native = unreal.get_default_object(unreal.ChunkedDestructibleGlobe)
shape_keys = ('meridian_count', 'parallel_count', 'shaft_width', 'meridian_thickness', 'parallel_thickness', 'curve_segments')
reference_defaults = {key: native.get_editor_property(key) for key in shape_keys}
for key, value in {**reference_defaults, **palette}.items():
    unreal.get_default_object(bp.generated_class()).set_editor_property(key, value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

stage = asset('M_GlobePreviewGround', folder + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
if not edit.get_material_property_input_node(stage, unreal.MaterialProperty.MP_BASE_COLOR):
    color = edit.create_material_expression(stage, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(.08,.09,.10,1))
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = edit.create_material_expression(stage, unreal.MaterialExpressionConstant)
    rough.set_editor_property('r', .95)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(stage)
    assert unreal.EditorAssetLibrary.save_loaded_asset(stage, False)

map_path = folder + '/Maps/Lvl_DestructibleGlobePreview'
if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    globe = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleGlobe))
else:
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    globe = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
    globe.set_actor_label('ChunkedDestructibleGlobe')
    globe.set_folder_path('Globe')
    for label, yaw, pitch, intensity in [('Key',-40,-45,4.), ('Fill',140,-25,1.7)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,4000), unreal.Rotator(pitch=pitch,yaw=yaw))
        light.set_actor_label('Preview_' + label); light.set_folder_path('Preview')
        light.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property('intensity', intensity)
        light.light_component.set_editor_property('cast_shadows', label == 'Key')
        light.light_component.set_editor_property('shadow_bias', .8)
        light.light_component.set_editor_property('shadow_slope_bias', .8)
        light.light_component.set_editor_property('light_source_angle', 2.)
    ground = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-26))
    ground.set_actor_label('Preview_Ground'); ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0, stage)
    ground.set_actor_scale3d(unreal.Vector(10000,10000,.5))
    ambient = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    ambient.set_actor_label('Preview_Ambient'); ambient.set_folder_path('Preview')
    ambient.set_editor_property('unbound', True)
    post = ambient.get_editor_property('settings')
    for key, value in {'ambient_cubemap': unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
                       'override_ambient_cubemap_intensity': True, 'ambient_cubemap_intensity': .6,
                       'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                       'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                       'override_auto_exposure_bias': True, 'auto_exposure_bias': .1}.items():
        post.set_editor_property(key, value)
    ambient.set_editor_property('settings', post)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,-2100,110), unreal.Rotator(yaw=90))
    start.set_folder_path('Preview')

# Light the open compartments from the viewing side, keeping the studio setup local to this map.
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if label in ('Preview_Key', 'Preview_Fill'):
        key = label == 'Preview_Key'
        actor.set_actor_rotation(unreal.Rotator(pitch=-42 if key else -25, yaw=135 if key else -50), False)
        actor.light_component.set_editor_property('intensity', 5. if key else 2.7)
    elif label == 'Preview_Ambient':
        post = actor.get_editor_property('settings')
        post.set_editor_property('ambient_cubemap_intensity', 1.2)
        actor.set_editor_property('settings', post)
for key, value in {**reference_defaults, **palette}.items():
    globe.set_editor_property(key, value)
globe.rebuild_globe()
location = unreal.Vector(1750,-2500,3100)
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    location, unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0,0,1550)))
actors.set_selected_level_actors([globe])
assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
assert globe.generated_meridian_pieces > 0 and globe.generated_parallel_pieces > 0 and not globe.generation_warning
result = {'blueprint': bp.get_path_name(), 'map': map_path, 'pieces': globe.generated_piece_count,
          'meridian_pieces': globe.generated_meridian_pieces, 'parallel_pieces': globe.generated_parallel_pieces,
          'height_cm': globe.generated_height, 'reference_defaults': reference_defaults}
(out/'Setup.json').write_text(json.dumps(result, indent=2))
unreal.log('DESTRUCTIBLE_GLOBE_SETUP_OK ' + str(result))
