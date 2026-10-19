"""Verify saved assets in a fresh process, newly spawned rigs, and independent photo/retro controls."""
import json
import math
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightMoonStyles'
upgrade = json.loads((out / 'upgrade.json').read_text())
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert len(actors.get_all_level_actors()) == upgrade['actor_count']
bp = unreal.load_asset(upgrade['blueprint'])
cdo = unreal.get_default_object(bp.generated_class())


def stable(value):
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return str(value).split('{', 1)[-1]


for path, saved in upgrade['preserved'].items():
    actor = next(a for a in actors.get_all_level_actors() if a.get_path_name() == path)
    actual = dict(label=actor.get_actor_label(), location=stable(actor.get_actor_location()),
                  rotation=stable(actor.get_actor_rotation()), scale=stable(actor.get_actor_scale3d()),
                  folder=str(actor.get_folder_path()), cls=actor.get_class().get_path_name())
    for key, expected in saved.items():
        value = actual[key] if key in actual else stable(actor.get_editor_property(key))
        assert value == expected, (key, value, expected)
    for key, expected in upgrade['settings'].items():
        assert stable(actor.get_editor_property(key)) == expected, key
        assert actor.get_editor_property(key) == cdo.get_editor_property(key), key

rig = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'RetroTwilightEnvironment')
texture = rig.get_editor_property('moon_texture')
assert isinstance(texture, unreal.Texture2D)
assert (texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) == (8192, 4096)
assert texture.get_editor_property('max_texture_size') == 8192
assert texture.get_editor_property('srgb')
material = rig.get_editor_property('sky_material')
expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
custom = next(n for n in expressions if isinstance(n, unreal.MaterialExpressionCustom))
assert custom.get_editor_property('code') == (Path(__file__).parent.parent / 'RetroTwilightSky.ush').read_text()
photo = next(n for n in expressions if isinstance(n, unreal.MaterialExpressionTextureObjectParameter))
assert str(photo.get_editor_property('parameter_name')) == 'MoonTexture'
assert photo.get_editor_property('texture') == texture

mid = rig.get_editor_property('sky_dome').get_material(0)
assert isinstance(mid, unreal.MaterialInstanceDynamic)
light = rig.get_editor_property('moon_light')
ambient = rig.get_editor_property('ambient_light')
lighting = (light.get_editor_property('intensity'), ambient.get_editor_property('intensity'))
patterns = [unreal.RetroMoonPattern.SMOOTH,
            unreal.RetroMoonPattern.RETRO_BANDS, unreal.RetroMoonPattern.REALISTIC]
for pattern in patterns:
    rig.set_editor_property('moon_pattern', pattern)
    rig.apply_environment()
    assert mid.get_scalar_parameter_value('MoonPattern') == pattern.value
assert mid.get_texture_parameter_value('MoonTexture') == texture
for prop, param, cases in [
    ('moon_emissive_intensity', 'MoonEmissiveIntensity', [(-1, 0), (0, 0), (1.35, 1.35), (3, 3)]),
    ('moon_retro_band_gap', 'MoonRetroBandGap', [(-1, 0), (0, 0), (1, 1), (5, 2)])
]:
    for value, expected in cases:
        rig.set_editor_property(prop, value)
        rig.apply_environment()
        assert abs(mid.get_scalar_parameter_value(param) - expected) < 1e-5
rig.set_editor_property('moon_texture_rotation', 90)
rig.apply_environment()
assert abs(mid.get_scalar_parameter_value('MoonTextureRotation') - math.pi / 2) < 1e-5
for prop, param in [('moon_retro_top_color', 'MoonRetroTopColor'), ('moon_retro_bottom_color', 'MoonRetroBottomColor')]:
    rig.set_editor_property(prop, unreal.LinearColor(0.13, 0.42, 0.76, 1))
    rig.apply_environment()
    color = mid.get_vector_parameter_value(param)
    assert max(abs(color.r - 0.13), abs(color.g - 0.42), abs(color.b - 0.76)) < 1e-5
rig.set_editor_property('moon_texture', None)
rig.apply_environment()
assert mid.get_texture_parameter_value('MoonTexture') == texture, 'Bundled texture fallback failed'
assert lighting == (light.get_editor_property('intensity'), ambient.get_editor_property('intensity'))

for cls in (unreal.RetroTwilightEnvironment, bp.generated_class()):
    spawned = actors.spawn_actor_from_class(cls, unreal.Vector(123, 456, 789), transient=True)
    defaults = unreal.get_default_object(cls)
    for key in upgrade['settings']:
        assert spawned.get_editor_property(key) == defaults.get_editor_property(key), key
    assert spawned.get_editor_property('moon_texture') == texture
    spawned.set_editor_property('moon_pattern', unreal.RetroMoonPattern.REALISTIC)
    spawned.apply_environment()
    spawned_mid = spawned.get_editor_property('sky_dome').get_material(0)
    assert isinstance(spawned_mid, unreal.MaterialInstanceDynamic)
    assert spawned_mid.get_texture_parameter_value('MoonTexture') == texture
    assert abs(spawned_mid.get_scalar_parameter_value('MoonEmissiveIntensity') - 1.35) < 1e-5
    actors.destroy_actor(spawned)
report = dict(passed=True, fresh_process=True, texture_resolution=[8192, 4096], saved_shader_matches=True,
              native_and_blueprint_spawns=True, styles=3, texture_fallback=True, lighting_independent=True,
              all_existing_settings_preserved=True)
(out / 'verification.json').write_text(json.dumps(report, indent=2))
unreal.log('MOON_STYLES_VERIFIED ' + json.dumps(report))
