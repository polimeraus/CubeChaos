"""Fresh-process moon persistence, material propagation and new-instance checks; never saves."""
import json
import math
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightMoon'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not world or world.get_name() != 'Lvl_FirstPerson':
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rigs = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment)]
assert len(rigs) == 1
rig = rigs[0]
upgrade = json.loads((out / 'upgrade.json').read_text())
assert len(actors.get_all_level_actors()) == upgrade['actor_count']
for key, value in upgrade['preserved'].items():
    if key in ('label', 'location', 'rotation', 'folder'):
        continue
    assert str(rig.get_editor_property(key)).split('{', 1)[-1] == value, key
assert rig.get_actor_label() == upgrade['preserved']['label']
assert str(rig.get_actor_location()).split('{', 1)[-1] == upgrade['preserved']['location']
assert str(rig.get_actor_rotation()).split('{', 1)[-1] == upgrade['preserved']['rotation']
assert str(rig.get_folder_path()) == upgrade['preserved']['folder']

bp = unreal.load_class(None, '/Game/Environment/RetroTwilight/BP_RetroTwilightEnvironment.BP_RetroTwilightEnvironment_C')
cdo = unreal.get_default_object(bp)
params = tuple(upgrade['settings'])
for key in params:
    assert rig.get_editor_property(key) == cdo.get_editor_property(key), key
assert rig.get_editor_property('moon_diameter') == 10.0
assert rig.get_editor_property('moon_pattern') == unreal.RetroMoonPattern.REALISTIC
assert rig.get_editor_property('moon_color_preset') == unreal.RetroMoonColor.PALE_YELLOW
mid = rig.get_editor_property('sky_dome').get_material(0)
assert isinstance(mid, unreal.MaterialInstanceDynamic)
assert abs(mid.get_scalar_parameter_value('MoonRadius') - math.radians(5)) < 1e-6
assert mid.get_scalar_parameter_value('MoonPattern') == 5
assert mid.get_scalar_parameter_value('MoonPhase') == 0
moon = rig.get_editor_property('moon_light')
base_light = moon.get_editor_property('intensity')
for diameter in (0.1, 4, 10, 30):
    rig.set_editor_property('moon_diameter', diameter)
    rig.apply_environment()
    assert abs(mid.get_scalar_parameter_value('MoonRadius') - math.radians(diameter / 2)) < 1e-6
patterns = [unreal.RetroMoonPattern.SMOOTH, unreal.RetroMoonPattern.RETRO_BANDS, unreal.RetroMoonPattern.REALISTIC]
for pattern in patterns:
    rig.set_editor_property('moon_pattern', pattern)
    rig.apply_environment()
    assert mid.get_scalar_parameter_value('MoonPattern') == pattern.value
for phase in (0, 25, 50, 75, 90, 100):
    rig.set_editor_property('moon_crescent_amount', phase)
    rig.apply_environment()
    assert abs(mid.get_scalar_parameter_value('MoonPhase') - phase / 100.0) < 1e-6
    assert moon.get_editor_property('intensity') == base_light
for rotation in (0, 90, 180, 270, 360):
    rig.set_editor_property('moon_phase_rotation', rotation)
    rig.apply_environment()
    assert abs(mid.get_scalar_parameter_value('MoonPhaseRotation') - math.radians(rotation)) < 1e-6
for preset, expected in [
    (unreal.RetroMoonColor.WHITE, (1, 1, 1)),
    (unreal.RetroMoonColor.PALE_YELLOW, (1, 0.88, 0.60)),
    (unreal.RetroMoonColor.PALE_ORANGE, (1, 0.65, 0.38)),
    (unreal.RetroMoonColor.CUSTOM, (0.3, 0.6, 1))
]:
    rig.set_editor_property('moon_custom_color', unreal.LinearColor(0.3, 0.6, 1, 1))
    rig.set_editor_property('moon_color_preset', preset)
    rig.apply_environment()
    color = mid.get_vector_parameter_value('MoonColor')
    assert all(abs(a-b) < 1e-6 for a, b in zip((color.r, color.g, color.b), expected))
for brightness in (0, 0.5, 2):
    rig.set_editor_property('moon_brightness', brightness)
    rig.apply_environment()
    assert mid.get_scalar_parameter_value('MoonBrightness') == brightness
for key in params:
    rig.set_editor_property(key, cdo.get_editor_property(key))
rig.apply_environment()
spawned = actors.spawn_actor_from_class(bp, unreal.Vector(1000, 2000, 3000), transient=True)
for key in params:
    assert spawned.get_editor_property(key) == cdo.get_editor_property(key), key
assert isinstance(spawned.get_editor_property('sky_dome').get_material(0), unreal.MaterialInstanceDynamic)
assert abs(spawned.get_editor_property('sky_dome').get_material(0).get_scalar_parameter_value('MoonRadius') - math.radians(5)) < 1e-6
actors.destroy_actor(spawned)
report = dict(passed=True, fresh_process=True, new_instance_defaults=True, unrelated_settings_preserved=True,
              patterns=3, colors=4, phase_steps=6, rotation_steps=5, size_steps=4, brightness_steps=3)
(out / 'verification.json').write_text(json.dumps(report, indent=2))
unreal.log('MOON_VERIFIED ' + json.dumps(report))
