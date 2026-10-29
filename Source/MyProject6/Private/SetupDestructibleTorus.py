"""Create the silver open-torus Blueprint and an isolated playable reference map."""
import json
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/Torus'
out = Path(unreal.Paths.project_saved_dir())/'Torus'
out.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
edit = unreal.MaterialEditingLibrary

def asset(name, directory, cls, factory):
    path = directory+'/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name, directory, cls, factory)

master = asset('M_TorusMetal', folder+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
if not edit.get_material_property_input_node(master, unreal.MaterialProperty.MP_BASE_COLOR):
    tint = edit.create_material_expression(master, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name', 'MetalColor')
    tint.set_editor_property('default_value', unreal.LinearColor(.46,.46,.46,1))
    edit.connect_material_property(tint, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    for name, value, prop in [('Metallic',.65,unreal.MaterialProperty.MP_METALLIC),
                              ('Roughness',.28,unreal.MaterialProperty.MP_ROUGHNESS)]:
        parameter = edit.create_material_expression(master, unreal.MaterialExpressionScalarParameter)
        parameter.set_editor_property('parameter_name', name); parameter.set_editor_property('default_value', value)
        edit.connect_material_property(parameter, '', prop)
    edit.recompile_material(master)
    assert unreal.EditorAssetLibrary.save_loaded_asset(master, False)
palette = {}
for name, key, color, rough in [('Silver','panel_material',(.46,.46,.46),.28),
                               ('Edge','edge_material',(.84,.84,.84),.23)]:
    material = asset('MI_Torus'+name, folder+'/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(material, master)
    edit.set_material_instance_vector_parameter_value(material, 'MetalColor', unreal.LinearColor(*color,1))
    edit.set_material_instance_scalar_parameter_value(material, 'Roughness', rough)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    palette[key] = material

factory = unreal.BlueprintFactory(); factory.set_editor_property('parent_class', unreal.ChunkedDestructibleTorus)
bp = asset('BP_ChunkedDestructibleTorus', folder, unreal.Blueprint, factory)
for key, value in palette.items():
    unreal.get_default_object(bp.generated_class()).set_editor_property(key, value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

stage = asset('M_TorusPreviewGround', folder+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
if not edit.get_material_property_input_node(stage, unreal.MaterialProperty.MP_BASE_COLOR):
    color = edit.create_material_expression(stage, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(.72,.73,.75,1))
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = edit.create_material_expression(stage, unreal.MaterialExpressionConstant); rough.set_editor_property('r', .9)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(stage)
    assert unreal.EditorAssetLibrary.save_loaded_asset(stage, False)

map_path = folder+'/Maps/Lvl_DestructibleTorusPreview'
if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    torus = next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleTorus))
else:
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    torus = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
    torus.set_actor_label('ChunkedDestructibleTorus'); torus.set_folder_path('Torus')
    for label, yaw, pitch, intensity in [('Key',60,-50,4.), ('Fill',-100,-30,1.5)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,4000), unreal.Rotator(pitch=pitch,yaw=yaw))
        light.set_actor_label('Preview_'+label); light.set_folder_path('Preview')
        light.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property('intensity', intensity)
        light.light_component.set_editor_property('cast_shadows', label=='Key')
        light.light_component.set_editor_property('shadow_bias', .8)
        light.light_component.set_editor_property('shadow_slope_bias', .8)
        light.light_component.set_editor_property('light_source_angle', 5.)
    ground = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-26))
    ground.set_actor_label('Preview_Ground'); ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0, stage)
    ground.set_actor_scale3d(unreal.Vector(10000,10000,.5))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,4000))
    sky.set_actor_label('Preview_Sky'); sky.set_folder_path('Preview')
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky_comp.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky_comp.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky_comp.set_editor_property('intensity', 1.5)
    ambient = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    ambient.set_actor_label('Preview_Ambient'); ambient.set_folder_path('Preview'); ambient.set_editor_property('unbound', True)
    post = ambient.get_editor_property('settings')
    for key, value in {'ambient_cubemap': unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
                       'override_ambient_cubemap_intensity': True, 'ambient_cubemap_intensity': .8,
                       'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                       'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                       'override_auto_exposure_bias': True, 'auto_exposure_bias': .1}.items():
        post.set_editor_property(key, value)
    ambient.set_editor_property('settings', post)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,-2900,110), unreal.Rotator(yaw=90))
    start.set_folder_path('Preview')

torus.rebuild_torus()
location = unreal.Vector(1900,-4400,2600)
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    location, unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0,0,650)))
actors.set_selected_level_actors([torus])
assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
assert torus.generated_piece_count > 1000 and not torus.generation_warning
result = {'blueprint': bp.get_path_name(), 'map': map_path, 'pieces': torus.generated_piece_count,
          'meridian_pieces': torus.generated_meridian_pieces, 'parallel_pieces': torus.generated_parallel_pieces,
          'height_cm': torus.generated_height, 'center_opening_width_cm': torus.center_opening_width}
(out/'Setup.json').write_text(json.dumps(result, indent=2))
unreal.log('DESTRUCTIBLE_TORUS_SETUP_OK '+str(result))
