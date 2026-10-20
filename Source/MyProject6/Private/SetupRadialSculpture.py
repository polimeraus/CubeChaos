"""Create the standalone sculpture Blueprint and its own playable preview map."""
import json
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/RadialSculpture'
output = Path(unreal.Paths.project_saved_dir()) / 'RadialSculpture'
output.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary
material_path = folder + '/Materials/M_SculptureWhite'
if not library.does_asset_exist(material_path):
    material = assets.create_asset('M_SculptureWhite', folder + '/Materials',
                                  unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(.88, .88, .86, 1))
    roughness = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, 180)
    roughness.set_editor_property('parameter_name', 'Roughness')
    roughness.set_editor_property('default_value', .86)
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(material)
    assert library.save_loaded_asset(material, False)
material = unreal.load_asset(material_path)
stage_path = folder + '/Materials/M_SculptureStage'
if not library.does_asset_exist(stage_path):
    stage = assets.create_asset('M_SculptureStage', folder + '/Materials',
                               unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    color = edit.create_material_expression(stage, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color.set_editor_property('constant', unreal.LinearColor(.12, .135, .15, 1))
    roughness = edit.create_material_expression(stage, unreal.MaterialExpressionConstant, -300, 180)
    roughness.set_editor_property('r', .92)
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(stage)
    assert library.save_loaded_asset(stage, False)
stage = unreal.load_asset(stage_path)
bp_path = folder + '/BP_DestructibleRadialSculpture'
if not library.does_asset_exist(bp_path):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.DestructibleRadialSculpture)
    bp = assets.create_asset('BP_DestructibleRadialSculpture', folder, unreal.Blueprint, factory)
    defaults = unreal.get_default_object(bp.generated_class())
    defaults.set_editor_property('sculpture_material', material)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert library.save_loaded_asset(bp, False)
bp = unreal.load_asset(bp_path)
map_path = folder + '/Maps/Lvl_RadialSculpturePreview'
if not library.does_asset_exist(map_path):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sculpture = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector())
    sculpture.set_actor_label('DestructibleRadialSculpture')
    assert sculpture.generated_piece_count > 1000
    for label, rotation, intensity in [
        ('Key', unreal.Rotator(pitch=-40, yaw=35, roll=0), 4.),
        ('Fill', unreal.Rotator(pitch=-18, yaw=-130, roll=0), 1.5)
    ]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2000), rotation)
        light.set_actor_label('Preview_' + label)
        light.set_folder_path('Preview')
        light.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property('intensity', intensity)
        if label == 'Fill':
            light.light_component.set_editor_property('cast_shadows', False)
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1500))
    sky.set_folder_path('Preview')
    sky.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.light_component.set_editor_property('intensity', .6)
    ground = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -15))
    ground.set_actor_label('Preview_Ground')
    ground.set_folder_path('Preview')
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.static_mesh_component.set_material(0, stage)
    ground.set_actor_scale3d(unreal.Vector(100, 100, .3))
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(1000, -1150, 120),
                                         unreal.Rotator(pitch=0, yaw=130, roll=0))
    start.set_folder_path('Preview')
    volume = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    volume.set_actor_label('Preview_Exposure')
    volume.set_folder_path('Preview')
    volume.set_editor_property('unbound', True)
    post = volume.get_editor_property('settings')
    for name, value in {
        'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
        'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.,
        'ambient_cubemap': unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'),
        'override_ambient_cubemap_intensity': True, 'ambient_cubemap_intensity': .35,
    }.items():
        post.set_editor_property(name, value)
    volume.set_editor_property('settings', post)
    location = unreal.Vector(1100, -1450, 1100)
    rotation = unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0, 0, 290))
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(location, rotation)
    actors.set_selected_level_actors([sculpture])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
# Keep this tool's own studio floor neutral on an existing preview as well.
world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ground = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'Preview_Ground')
if ground.static_mesh_component.get_material(0) != stage:
    ground.static_mesh_component.set_material(0, stage)
    ground.set_actor_scale3d(unreal.Vector(100, 100, .3))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
(output / 'Setup.json').write_text(json.dumps({'blueprint': bp_path, 'map': map_path, 'piece_size_cm': 40.}, indent=2))
unreal.log('RADIAL_SCULPTURE_SETUP_OK')
