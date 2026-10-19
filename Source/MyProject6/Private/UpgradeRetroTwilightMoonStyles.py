"""Bundle NASA imagery and persist the photographic/gradient moon controls in the owning class."""
import json
import shutil
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).parent))
from BuildRetroTwilightMaterial import build_material

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightMoonStyles'
out.mkdir(parents=True, exist_ok=True)
content = Path(unreal.Paths.project_content_dir())
backup = out / 'BeforeUpgrade' / 'Content'
if not backup.exists():
    backup.mkdir(parents=True)
    for relative in ('Environment/RetroTwilight', '__ExternalActors__/FirstPerson/Lvl_FirstPerson'):
        shutil.copytree(content / relative, backup / relative)
    (backup / 'FirstPerson').mkdir()
    shutil.copy2(content / 'FirstPerson/Lvl_FirstPerson.umap', backup / 'FirstPerson/Lvl_FirstPerson.umap')

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
target = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'RetroTwilightEnvironment')
assert isinstance(target, unreal.RetroTwilightEnvironment)
owner_path = target.get_class().get_path_name().split('.')[0]
bp = unreal.load_asset(owner_path)
assert isinstance(bp, unreal.Blueprint), owner_path
new_keys = ['moon_texture', 'moon_emissive_intensity', 'moon_texture_rotation',
            'moon_retro_top_color', 'moon_retro_bottom_color', 'moon_retro_band_gap']
preserve = ['dominant_color', 'light_intensity', 'sky_brightness', 'ambient_intensity',
            'moon_light_intensity', 'show_moon', 'moon_elevation', 'moon_azimuth', 'moon_diameter',
            'moon_pattern', 'moon_crescent_amount', 'moon_phase_rotation', 'moon_color_preset',
            'moon_custom_color', 'moon_brightness', 'star_brightness', 'fog_density', 'fog_start_distance',
            'fog_height', 'bloom_intensity', 'exposure_compensation', 'is_spatially_loaded',
            'moon_surface_contrast',
            'moon_earthshine', 'moon_glow_color', 'moon_glow_intensity', 'moon_glow_width', 'moon_glow_softness',
            'show_aurora', 'aurora_lower_color', 'aurora_upper_color', 'aurora_accent_color',
            'aurora_intensity', 'aurora_density', 'aurora_width', 'aurora_elevation', 'aurora_azimuth',
            'aurora_coverage', 'aurora_flow_speed', 'aurora_turbulence', 'aurora_detail_scale', 'aurora_seed']


def stable(value):
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return str(value).split('{', 1)[-1]


def snapshot(actor):
    result = {key: stable(actor.get_editor_property(key)) for key in preserve}
    result.update(label=actor.get_actor_label(), location=stable(actor.get_actor_location()),
                  rotation=stable(actor.get_actor_rotation()), scale=stable(actor.get_actor_scale3d()),
                  folder=str(actor.get_folder_path()), cls=actor.get_class().get_path_name())
    return result


rigs = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment)]
before = {a.get_path_name(): snapshot(a) for a in rigs}
material = build_material()
texture = unreal.load_asset('/Game/Environment/RetroTwilight/T_Moon_NASA_LRO_8K')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.RetroTwilightEnvironment)
settings = {key: native.get_editor_property(key) for key in new_keys}
settings['moon_texture'] = texture
cdo.set_editor_property('sky_material', material)
for key, value in settings.items():
    cdo.set_editor_property(key, value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
packages = []
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.RetroTwilightEnvironment):
        if actor.get_class() == bp.generated_class():
            for key, value in settings.items():
                actor.set_editor_property(key, value)
            actor.apply_environment()
            packages.append(actor.get_package())
        assert snapshot(actor) == before[actor.get_path_name()], actor.get_actor_label()
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_packages(packages, False)
report = dict(passed=True, blueprint=owner_path, preserved=before,
              settings={key: stable(value) for key, value in settings.items()},
              actor_count=len(actors.get_all_level_actors()), source='https://svs.gsfc.nasa.gov/4720/')
(out / 'upgrade.json').write_text(json.dumps(report, indent=2))
unreal.log('MOON_STYLES_UPGRADE_OK ' + json.dumps(report))
