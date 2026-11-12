"""Create the ParametricPlatform-only city Blueprint and an isolated, saved demo level."""
import json
from pathlib import Path
import unreal

ROOT = '/Game/Procedural/PlatformCity'
BP = ROOT + '/BP_ParametricPlatformCity'
MAP = ROOT + '/Maps/L_PlatformCity_Demo'
out = Path(unreal.Paths.project_saved_dir()) / 'PlatformCity'
out.mkdir(parents=True, exist_ok=True)
assets = unreal.AssetToolsHelpers.get_asset_tools()
if not unreal.EditorAssetLibrary.does_asset_exist(BP):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ParametricPlatformCity)
    blueprint = assets.create_asset('BP_ParametricPlatformCity', ROOT, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
else:
    blueprint = unreal.load_asset(BP)

# Store authoring settings on the class; no file path or source actor state is copied.
native = unreal.get_default_object(unreal.ParametricPlatformCity)
defaults = unreal.get_default_object(blueprint.generated_class())
defaults.set_editor_property('rules', native.get_editor_property('rules'))
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
    city = actors.spawn_actor_from_class(blueprint.generated_class(), unreal.Vector())
    city.set_actor_label('ParametricPlatformCity')
    assert city.generate_with_seed(11022026), city.last_error
    for rotation, intensity in [(unreal.Rotator(pitch=-60, yaw=-35), 5), (unreal.Rotator(pitch=-35, yaw=140), 2)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 5000), rotation)
        light.light_component.set_editor_property('intensity', intensity)
    location = unreal.Vector(0, -4700, 6500)
    rotation = unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector())
    start = actors.spawn_actor_from_class(unreal.PlayerStart, location, rotation)
    start.set_actor_label('CityOverview_FreeFlyStart')
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(location, rotation)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
else:
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    city = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricPlatformCity))
    city.set_editor_property('rules', defaults.get_editor_property('rules'))
    assert city.generate_with_seed(11022026), city.last_error
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)

report = {'blueprint': BP, 'map': MAP, 'seed': city.seed, 'signature': city.generated_plan.signature,
          'platform_count': len(city.get_platforms()), 'report': city.generation_report,
          'width': city.rules.width, 'height': city.rules.height, 'corner_radius': city.rules.corner_radius}
(out / 'Created.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('PLATFORM_CITY_CREATED ' + json.dumps(report))
