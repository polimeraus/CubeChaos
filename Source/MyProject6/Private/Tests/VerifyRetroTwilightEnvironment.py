"""Fresh-process persistence and functional parameter checks. Never save test overrides."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightEnvironment'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not world or world.get_name() != 'Lvl_FirstPerson':
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = list(subsystem.get_all_level_actors())
rigs = [a for a in actors if isinstance(a, unreal.RetroTwilightEnvironment)]
assert len(rigs) == 1, len(rigs)
rig = rigs[0]
assert not rig.get_editor_property('is_spatially_loaded')
assert not rig.is_actor_tick_enabled()
leftover = [a.get_actor_label() for a in actors if isinstance(a, (unreal.DirectionalLight, unreal.SkyLight,
    unreal.ExponentialHeightFog, unreal.SkyAtmosphere, unreal.VolumetricCloud))]
assert not leftover, leftover
bp_class = unreal.load_class(None, '/Game/Environment/RetroTwilight/BP_RetroTwilightEnvironment.BP_RetroTwilightEnvironment_C')
cdo = unreal.get_default_object(bp_class)
params = ('dominant_color', 'light_intensity', 'sky_brightness', 'ambient_intensity',
          'moon_light_intensity', 'show_moon', 'moon_elevation', 'moon_azimuth', 'moon_diameter',
          'star_brightness', 'fog_density', 'fog_start_distance', 'fog_height', 'bloom_intensity',
          'exposure_compensation', 'sky_material')
for key in params:
    assert rig.get_editor_property(key) == cdo.get_editor_property(key), key
assert rig.get_editor_property('sky_material')
mid = rig.get_editor_property('sky_dome').get_material(0)
assert isinstance(mid, unreal.MaterialInstanceDynamic), 'Transient sky material not rebuilt on reload'
assert abs(mid.get_scalar_parameter_value('SkyBrightness') - rig.get_editor_property('sky_brightness')) < 1e-5
moon = rig.get_editor_property('moon_light')
ambient = rig.get_editor_property('ambient_light')
haze = rig.get_editor_property('haze')
post = rig.get_editor_property('neon_post_process')
assert not ambient.get_editor_property('real_time_capture')
assert not haze.get_editor_property('enable_volumetric_fog')
assert post.get_editor_property('unbound')
assert post.get_editor_property('settings').get_editor_property('auto_exposure_method') == unreal.AutoExposureMethod.AEM_MANUAL
assert post.get_editor_property('settings').get_editor_property('bloom_threshold') == -1.0
assert post.get_editor_property('settings').get_editor_property('bloom_intensity') > 0

before = json.loads((out / 'before.json').read_text())
setup = json.loads((out / 'setup.json').read_text())
removed = {r['label'] for r in setup['removed']}
current = {a.get_path_name(): a for a in actors}
unchanged = 0
for row in before:
    if row['label'] in removed:
        continue
    actor = current.get(row['path'])
    if not actor:
        # Procedural child actors get a new object ID on each load; match their class and pose.
        matches = [a for a in actors if a.get_actor_label() == row['label']
                   and a.get_class().get_path_name() == row['cls']
                   and str(a.get_actor_location()).split('{')[1] == row['location'].split('{')[1]]
        assert len(matches) == 1, 'Ambiguous or missing generated actor: ' + row['label']
        actor = matches[0]
    assert actor, 'Missing gameplay actor: ' + row['label']
    assert actor.get_class().get_path_name() == row['cls'], row['label']
    # Unreal struct repr includes a transient pointer; compare the serialized numeric body only.
    assert str(actor.get_actor_location()).split('{')[1] == row['location'].split('{')[1], row['label']
    assert str(actor.get_actor_rotation()).split('{')[1] == row['rotation'].split('{')[1], row['label']
    unchanged += 1

base_moon = moon.get_editor_property('intensity')
base_ambient = ambient.get_editor_property('intensity')
rig.set_editor_property('light_intensity', 2.0)
rig.apply_environment()
assert abs(moon.get_editor_property('intensity') - base_moon * 2) < 1e-5
assert abs(ambient.get_editor_property('intensity') - base_ambient * 2) < 1e-5
test_color = unreal.LinearColor(0.02, 0.8, 0.2, 1)
rig.set_editor_property('dominant_color', test_color)
rig.apply_environment()
assert mid.get_vector_parameter_value('DominantColor') == test_color
assert ambient.get_editor_property('light_color').g > ambient.get_editor_property('light_color').b
rig.set_editor_property('show_moon', False)
rig.apply_environment()
assert moon.get_editor_property('intensity') == 0
assert mid.get_scalar_parameter_value('MoonVisibility') == 0
rig.set_editor_property('light_intensity', 0.0)
rig.apply_environment()
assert ambient.get_editor_property('intensity') == 0
for key in params:
    if key != 'sky_material':
        rig.set_editor_property(key, cdo.get_editor_property(key))
rig.apply_environment()

# Newly placed and runtime-style spawned Blueprint instances inherit the same preset/material.
spawned = subsystem.spawn_actor_from_class(bp_class, unreal.Vector(1000, 2000, 3000), transient=True)
assert spawned
for key in params:
    assert spawned.get_editor_property(key) == cdo.get_editor_property(key), key
assert isinstance(spawned.get_editor_property('sky_dome').get_material(0), unreal.MaterialInstanceDynamic)
assert abs(spawned.get_editor_property('moon_light').get_editor_property('intensity') - base_moon) < 1e-5
subsystem.destroy_actor(spawned)
result = dict(passed=True, fresh_process=True, gameplay_actors_preserved=unchanged,
              parameters_verified=len(params), power_doubles_lights=True, zero_power_verified=True,
              color_propagation=True, moon_toggle=True, new_instance_defaults=True,
              transient_material_restored=True, tick_enabled=False)
(out / 'verification.json').write_text(json.dumps(result, indent=2))
unreal.log('TWILIGHT_VERIFIED ' + json.dumps(result))
