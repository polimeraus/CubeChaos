"""Fresh-process persistence, parameter propagation, clamping and new-instance checks; never saves."""
import json
import math
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightAurora'
upgrade = json.loads((out / 'upgrade.json').read_text())
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert len(actors.get_all_level_actors()) == upgrade['actor_count']
rig = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'RetroTwilightEnvironment')
bp = unreal.load_asset(upgrade['blueprint'])
cdo = unreal.get_default_object(bp.generated_class())


def stable(value):
    return str(value).split('{', 1)[-1]


for path, saved in upgrade['preserved'].items():
    actor = next(a for a in actors.get_all_level_actors() if a.get_path_name() == path)
    actual = dict(label=actor.get_actor_label(), location=stable(actor.get_actor_location()),
                  rotation=stable(actor.get_actor_rotation()), scale=stable(actor.get_actor_scale3d()),
                  folder=str(actor.get_folder_path()), cls=actor.get_class().get_path_name())
    for key, value in saved.items():
        assert actual.get(key, stable(actor.get_editor_property(key)) if key not in actual else None) == value, key
    if actor.get_class() == bp.generated_class():
        for key, value in upgrade['settings'].items():
            assert stable(actor.get_editor_property(key)) == value, key
            assert actor.get_editor_property(key) == cdo.get_editor_property(key), key

material = rig.get_editor_property('sky_material')
custom = next(n for n in unreal.MaterialEditingLibrary.get_material_expressions(material)
              if isinstance(n, unreal.MaterialExpressionCustom))
shader_path = Path(__file__).parent.parent / 'RetroTwilightSky.ush'
assert custom.get_editor_property('code') == shader_path.read_text(), 'Saved shader differs from source'
assert any(isinstance(n, unreal.MaterialExpressionTime)
           for n in unreal.MaterialEditingLibrary.get_material_expressions(material)), 'No animation clock'
mid = rig.get_editor_property('sky_dome').get_material(0)
assert isinstance(mid, unreal.MaterialInstanceDynamic)
light = rig.get_editor_property('moon_light')
ambient = rig.get_editor_property('ambient_light')
lighting = (light.get_editor_property('intensity'), ambient.get_editor_property('intensity'))

cases = [
    ('aurora_intensity', 'AuroraIntensity', [(0, 0), (2.5, 2.5), (-1, 0)]),
    ('aurora_density', 'AuroraDensity', [(0, 0), (1.6, 1.6), (5, 2)]),
    ('aurora_flow_speed', 'AuroraFlowSpeed', [(0, 0), (-0.4, -0.4), (0.8, 0.8)]),
    ('aurora_turbulence', 'AuroraTurbulence', [(0, 0), (1.7, 1.7), (-2, 0)]),
    ('aurora_detail_scale', 'AuroraDetailScale', [(0, 0.25), (2, 2), (8, 4)]),
    ('aurora_seed', 'AuroraSeed', [(0, 0), (83, 83), (2000, 1000)]),
    ('moon_glow_intensity', 'MoonGlowIntensity', [(0, 0), (2, 2), (-2, 0)]),
    ('moon_glow_width', 'MoonGlowWidth', [(0, 0.1), (4, 4), (10, 8)]),
    ('moon_glow_softness', 'MoonGlowSoftness', [(0, 0.25), (2.7, 2.7), (8, 4)]),
    ('moon_surface_contrast', 'MoonSurfaceContrast', [(0, 0), (1.8, 1.8), (-1, 0)]),
    ('moon_earthshine', 'MoonEarthshine', [(0, 0), (0.1, 0.1), (1, 0.2)]),
]
for prop, param, values in cases:
    for value, expected in values:
        rig.set_editor_property(prop, value)
        rig.apply_environment()
        assert abs(mid.get_scalar_parameter_value(param) - expected) < 1e-5, (prop, value)
for prop, param, degrees in [('aurora_width', 'AuroraWidth', 22),
                             ('aurora_elevation', 'AuroraElevation', 55),
                             ('aurora_azimuth', 'AuroraAzimuth', -90),
                             ('aurora_coverage', 'AuroraCoverage', 360)]:
    rig.set_editor_property(prop, degrees)
    rig.apply_environment()
    assert abs(mid.get_scalar_parameter_value(param) - math.radians(degrees)) < 1e-5, prop
for prop, param in [('aurora_lower_color', 'AuroraLowerColor'), ('aurora_upper_color', 'AuroraUpperColor'),
                    ('aurora_accent_color', 'AuroraAccentColor'), ('moon_glow_color', 'MoonGlowColor')]:
    rig.set_editor_property(prop, unreal.LinearColor(0.13, 0.42, 0.76, 1))
    rig.apply_environment()
    color = mid.get_vector_parameter_value(param)
    assert max(abs(color.r - 0.13), abs(color.g - 0.42), abs(color.b - 0.76)) < 1e-5, prop
for visible in (False, True):
    rig.set_editor_property('show_aurora', visible)
    rig.apply_environment()
    assert mid.get_scalar_parameter_value('AuroraVisibility') == float(visible)
assert lighting == (light.get_editor_property('intensity'), ambient.get_editor_property('intensity'))

for cls in (unreal.RetroTwilightEnvironment, bp.generated_class()):
    spawned = actors.spawn_actor_from_class(cls, unreal.Vector(123, 456, 789), transient=True)
    defaults = unreal.get_default_object(cls)
    for key in upgrade['settings']:
        assert spawned.get_editor_property(key) == defaults.get_editor_property(key), key
    spawned_mid = spawned.get_editor_property('sky_dome').get_material(0)
    assert isinstance(spawned_mid, unreal.MaterialInstanceDynamic)
    assert spawned_mid.get_scalar_parameter_value('AuroraVisibility') == 1
    assert abs(spawned_mid.get_scalar_parameter_value('AuroraIntensity') - 0.85) < 1e-5
    actors.destroy_actor(spawned)

report = dict(passed=True, fresh_process=True, saved_shader_matches=True, native_and_blueprint_spawns=True,
              scalar_checks=sum(len(c[2]) for c in cases) + 4, color_checks=5,
              visibility_checks=2, unrelated_settings_preserved=True, lighting_independent=True)
(out / 'verification.json').write_text(json.dumps(report, indent=2))
unreal.log('AURORA_VERIFIED ' + json.dumps(report))
