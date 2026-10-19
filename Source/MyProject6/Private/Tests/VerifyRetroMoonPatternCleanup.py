"""Fresh-process verification of style removal, persisted migration and material parameter IDs."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'TrimMoonPatterns'
expected=json.loads((out/'migration.json').read_text())
assert expected['passed']
world=unreal.EditorLoadingAndSavingUtils.load_map(expected['map'])
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(expected['blueprint'])
assert len(actors.get_all_level_actors())==expected['actor_count']
P=unreal.RetroMoonPattern
assert {p.name:p.value for p in P}=={'SMOOTH':0,'RETRO_BANDS':4,'REALISTIC':5}
for retired in ('ORIGINAL','MARBLED','CRATERED'):assert not hasattr(P,retired)

def stable(value):
    return value.get_path_name() if isinstance(value,unreal.Object) else str(value).split('{',1)[-1]

def settings(actor):return {k:stable(actor.get_editor_property(k)) for k in expected['class_defaults']}

assert settings(unreal.get_default_object(bp.generated_class()))==expected['class_defaults']
for actor in actors.get_all_level_actors():
    if actor.get_path_name() not in expected['placed']:continue
    saved=expected['placed'][actor.get_path_name()]
    actual=dict(settings=settings(actor),label=actor.get_actor_label(),location=stable(actor.get_actor_location()),
                rotation=stable(actor.get_actor_rotation()),scale=stable(actor.get_actor_scale3d()),
                folder=str(actor.get_folder_path()),cls=actor.get_class().get_path_name())
    assert actual==saved,(actual,saved)

material=unreal.load_asset('/Game/Environment/RetroTwilight/M_RetroTwilightSky')
expressions=unreal.MaterialEditingLibrary.get_material_expressions(material)
custom=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionCustom))
assert custom.get_editor_property('code')==(Path(__file__).parent.parent/'RetroTwilightSky.ush').read_text()
inputs={str(pin.get_editor_property('input_name')) for pin in custom.get_editor_property('inputs')}
assert not inputs.intersection({'MoonDetailStrength','MoonExoticTint','MoonMineralColor'})
for cls in (unreal.RetroTwilightEnvironment,bp.generated_class()):
    spawned=actors.spawn_actor_from_class(cls,unreal.Vector(123,456,789),transient=True)
    defaults=unreal.get_default_object(cls)
    assert spawned.get_editor_property('moon_pattern')==defaults.get_editor_property('moon_pattern')
    assert spawned.get_editor_property('moon_pattern') in tuple(P)
    illumination=spawned.get_editor_property('moon_light').get_editor_property('intensity')
    for pattern in P:
        spawned.set_editor_property('moon_pattern',pattern)
        spawned.apply_environment()
        mid=spawned.get_editor_property('sky_dome').get_material(0)
        assert mid.get_scalar_parameter_value('MoonPattern')==pattern.value
        assert mid.get_texture_parameter_value('MoonTexture')==spawned.get_editor_property('moon_texture')
        assert spawned.get_editor_property('moon_light').get_editor_property('intensity')==illumination
    actors.destroy_actor(spawned)
report=dict(passed=True,fresh_process=True,styles=['Smooth','Retro Bands','Realistic'],
            removed_styles_absent=True,unrelated_settings_and_transforms_preserved=True,
            native_and_blueprint_spawns=True,saved_material_matches=True)
(out/'verification.json').write_text(json.dumps(report,indent=2))
unreal.log('MOON_PATTERN_CLEANUP_VERIFIED')
if '-MoonPatternCleanupPreview' in unreal.SystemLibrary.get_command_line():
    preview=Path(__file__).parent/'RenderRetroTwilightMoonStyles.py'
    exec(compile(preview.read_text(),str(preview),'exec'))
