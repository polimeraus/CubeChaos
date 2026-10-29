"""Create the reusable voxel Blueprint and a separate six-shape preview map."""
import json
from pathlib import Path
import unreal

exec(compile((Path(__file__).parent / 'CreateVoxelShapeMaterial.py').read_text(), 'CreateVoxelShapeMaterial.py', 'exec'))

folder = '/Game/Structures/Parametric/VoxelShape'
output = Path(unreal.Paths.project_saved_dir()) / 'VoxelShape'
output.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp_path = folder + '/BP_ParametricVoxelShape'
if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
    bp = unreal.load_asset(bp_path)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ParametricVoxelShape)
    bp = assets.create_asset('BP_ParametricVoxelShape', folder, unreal.Blueprint, factory)
assert bp
unreal.get_default_object(bp.generated_class()).set_editor_property('shape_material', unreal.load_asset(folder + '/Materials/M_VoxelShapeGlow'))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
cdo = unreal.get_default_object(bp.generated_class())
assert cdo.shape_material
parameters = [str(p) for p in unreal.MaterialEditingLibrary.get_vector_parameter_names(cdo.shape_material)]
assert 'Color' in parameters, parameters

map_path = folder + '/Maps/Lvl_VoxelShapePreview'
if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.ParametricVoxelShape):
            actor.set_editor_property('shape_material', cdo.shape_material)
            actor.rebuild_shape()
else:
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    presets = [
        ('Heart', unreal.VoxelShapePreset.HEART, (1., .025, .32), 15),
        ('InvaderCrab', unreal.VoxelShapePreset.INVADER_CRAB, (.02, .8, 1.), 11),
        ('InvaderSquid', unreal.VoxelShapePreset.INVADER_SQUID, (.35, .08, 1.), 8),
        ('Ghost', unreal.VoxelShapePreset.GHOST, (1., .08, .14), 11),
        ('Chomper', unreal.VoxelShapePreset.CHOMPER, (1., .65, .015), 13),
        ('Star', unreal.VoxelShapePreset.STAR, (.02, 1., .3), 13),
    ]
    for i, (name, preset, color, width) in enumerate(presets):
        location = unreal.Vector((i % 3 - 1) * 900, 0, 1100 if i < 3 else 250)
        actor = actors.spawn_actor_from_class(bp.generated_class(), location)
        actor.set_actor_label('Voxel_' + name)
        actor.set_folder_path('Voxel Shapes')
        actor.configure_shape(preset, width, 2)
        actor.set_editor_property('color', unreal.LinearColor(*color, 1.))
        if i > 0:
            actor.set_editor_property('cube_size', 600. / width)
        actor.rebuild_shape()
        assert actor.generated_cube_count > 0 and not actor.generation_warning

    volume = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    volume.set_actor_label('Preview_Exposure'); volume.set_folder_path('Preview')
    volume.set_editor_property('unbound', True)
    post = volume.get_editor_property('settings')
    for key, value in {
        'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
        'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.,
        'override_bloom_intensity': True, 'bloom_intensity': .65,
    }.items():
        post.set_editor_property(key, value)
    volume.set_editor_property('settings', post)

    position = unreal.Vector(1450, -5300, 1950)
    rotation = unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 680))
    camera = actors.spawn_actor_from_class(unreal.CameraActor, position, rotation)
    camera.set_actor_label('Preview_Camera'); camera.set_folder_path('Preview')
    camera.camera_component.set_editor_property('field_of_view', 38.)
    camera.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.PLAYER0)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, position, rotation)
    start.set_folder_path('Preview')
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)

position = unreal.Vector(1450, -5300, 1950)
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    position, unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 680)))
heart = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'Voxel_Heart')
actors.set_selected_level_actors([heart])
assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
result = {'blueprint': bp_path, 'map': map_path, 'heart_cubes': heart.generated_cube_count,
          'heart_edges': heart.generated_edge_count, 'material_parameters': parameters}
(output / 'Setup.json').write_text(json.dumps(result, indent=2))
unreal.log('VOXEL_SHAPE_SETUP_OK ' + str(result))
