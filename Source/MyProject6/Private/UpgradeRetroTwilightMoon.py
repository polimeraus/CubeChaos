"""Upgrade the existing rig's moon without replacing actors or changing the scene's lighting."""
import json
import shutil
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).parent))
from BuildRetroTwilightMaterial import build_material

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightMoon'
out.mkdir(parents=True, exist_ok=True)
content = Path(unreal.Paths.project_content_dir())
backup = out / 'BeforeMoon'
if not backup.exists():
    backup.mkdir()
    shutil.copytree(content / 'Environment/RetroTwilight', backup / 'Assets')
    shutil.copytree(content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson', backup / 'ExternalActors')
    shutil.copy2(content / 'FirstPerson/Lvl_FirstPerson.umap', backup / 'Lvl_FirstPerson.umap')

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rigs = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment)]
assert len(rigs) == 1
rig = rigs[0]
preserve = ['dominant_color', 'light_intensity', 'sky_brightness', 'ambient_intensity',
            'moon_light_intensity', 'show_moon', 'moon_elevation', 'moon_azimuth', 'star_brightness',
            'fog_density', 'fog_start_distance', 'fog_height', 'bloom_intensity', 'exposure_compensation']
def stable(value):
    return str(value).split('{', 1)[-1]

before = {key: stable(rig.get_editor_property(key)) for key in preserve}
before.update(label=rig.get_actor_label(), location=stable(rig.get_actor_location()),
              rotation=stable(rig.get_actor_rotation()), folder=str(rig.get_folder_path()))

material = build_material()
bp = unreal.load_asset('/Game/Environment/RetroTwilight/BP_RetroTwilightEnvironment')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
settings = dict(moon_diameter=10.0, moon_pattern=unreal.RetroMoonPattern.REALISTIC,
                moon_crescent_amount=0.0, moon_phase_rotation=0.0,
                moon_color_preset=unreal.RetroMoonColor.PALE_YELLOW,
                moon_custom_color=unreal.LinearColor(1.0, 0.88, 0.60, 1.0), moon_brightness=1.0)
cdo.set_editor_property('sky_material', material)
for key, value in settings.items():
    cdo.set_editor_property(key, value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
# Blueprint compilation can reinstate existing instances. Resolve the actor again.
rig = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment))
for key, value in settings.items():
    rig.set_editor_property(key, value)
rig.apply_environment()
after = {key: stable(rig.get_editor_property(key)) for key in preserve}
after.update(label=rig.get_actor_label(), location=stable(rig.get_actor_location()),
             rotation=stable(rig.get_actor_rotation()), folder=str(rig.get_folder_path()))
assert before == after, 'An unrelated environment property changed'
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
report = dict(passed=True, preserved=before, settings={k: str(v) for k, v in settings.items()},
              actor_count=len(actors.get_all_level_actors()))
(out / 'upgrade.json').write_text(json.dumps(report, indent=2))
unreal.log('MOON_UPGRADE_OK ' + json.dumps(report))
