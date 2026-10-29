"""Create the unified Blueprint and a separate, persistent two-mode example map."""
import json
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform'
BP = ROOT + '/BP_ParametricPlatform'
MAP = ROOT + '/Maps/L_ParametricPlatformExamples'
out = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform'
out.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
if not unreal.EditorAssetLibrary.does_asset_exist(BP):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ParametricPlatform)
    blueprint = assets.create_asset('BP_ParametricPlatform', ROOT, unreal.Blueprint, factory)
    assert blueprint
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
else:
    blueprint = unreal.load_asset(BP)

records = []
if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    shape_names = ['GRID', 'TRIANGLE', 'ROUNDED_BLOB', 'SQUARE', 'CIRCLE', 'STAR', 'HEXAGON']
    patterns = ['SOLID', 'CHECKERBOARD', 'HYPNOTIC_SPIRAL', 'HEART_MOTIF', 'FLOWER_MOTIF', 'SOLID', 'STRIPES_X']
    for row, mode in enumerate([unreal.PlatformMode.SOLID, unreal.PlatformMode.CELLULAR]):
        for col, (shape_name, pattern_name) in enumerate(zip(shape_names, patterns)):
            position = unreal.Vector((col - 3) * 1500, (row - .5) * 1900, 0)
            actor = actors.spawn_actor_from_class(blueprint.generated_class(), position)
            label = ('Solid_' if row == 0 else 'Cellular_') + shape_name.title()
            actor.set_actor_label(label)
            actor.set_folder_path('Solid Platforms' if row == 0 else 'Cellular Platforms')
            actor.set_editor_property('platform_mode', mode)
            actor.set_editor_property('outer_shape', getattr(unreal.ChunkedDecorativeFloorShape, shape_name))
            actor.set_editor_property('shape_size', unreal.Vector2D(1100, 1000))
            actor.set_editor_property('thickness', 100)
            actor.set_editor_property('tile_size', unreal.Vector2D(125, 125))
            actor.set_editor_property('floor_pattern', getattr(unreal.ChunkedFloorPattern, pattern_name))
            actor.set_editor_property('boundary_inset', 60)
            actor.set_editor_property('border_width', 32)
            actor.set_editor_property('border_style', unreal.PlatformBorder.INSET_BAND)
            actor.set_editor_property('palette_tints', [unreal.LinearColor(.065, .11, .17, 1),
                                                       unreal.LinearColor(1, .5, .04, 1), unreal.LinearColor(.18, .55, .8, 1)])
            for key, size_key in [('heart_motif_settings', 'size'), ('star_motif_settings', 'size'),
                                  ('flower_motif_settings', 'flower_size')]:
                settings = actor.get_editor_property(key)
                settings.set_editor_property(size_key, unreal.Vector2D(650, 650))
                actor.set_editor_property(key, settings)
            actor.rebuild_platform()
            records.append({'label': label, 'shape': shape_name, 'pattern': pattern_name,
                            'mode': 'SOLID' if row == 0 else 'CELLULAR', 'location': [position.x, position.y, position.z]})
    for rotation, intensity in [(unreal.Rotator(pitch=-65, yaw=-30), 5), (unreal.Rotator(pitch=-35, yaw=150), 2)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 4000), rotation)
        light.light_component.set_editor_property('intensity', intensity)
    assert unreal.EditorLoadingAndSavingUtils.save_map(unreal.EditorLevelLibrary.get_editor_world(), MAP)
    (out / 'ExpectedExamples.json').write_text(json.dumps(records, indent=2))

unreal.log('PARAMETRIC_PLATFORM_CREATED ' + BP + ' MAP=' + MAP)
