"""Capture both new looks and control comparisons using the real renderer; never save previews."""
import json
import math
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightMoonStyles'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'Lvl_FirstPerson'
for name in ('PostProcessing', 'Bloom', 'Tonemapper'):
    unreal.SystemLibrary.execute_console_command(world, 'ShowFlag.' + name + ' 1')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rig = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'RetroTwilightEnvironment')
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 355), transient=True)
component = camera.get_component_by_class(unreal.CameraComponent)
P = unreal.RetroMoonPattern
views = [
    ('retro_reference', 36, dict(moon_pattern=P.RETRO_BANDS, moon_crescent_amount=0, moon_brightness=1.2, show_aurora=False)),
    ('retro_no_glow', 36, dict(moon_pattern=P.RETRO_BANDS, moon_crescent_amount=0, moon_brightness=1.2, show_aurora=False, moon_glow_intensity=0, bloom_intensity=0)),
    ('retro_halo_no_bloom', 36, dict(moon_pattern=P.RETRO_BANDS, moon_crescent_amount=0, moon_brightness=1.2, show_aurora=False, bloom_intensity=0)),
    ('realistic_full', 22, dict(moon_pattern=P.REALISTIC, moon_crescent_amount=0, moon_brightness=1.2, show_aurora=False)),
    ('realistic_emissive_off', 22, dict(moon_pattern=P.REALISTIC, moon_crescent_amount=0, moon_brightness=1.2, show_aurora=False, moon_emissive_intensity=0, moon_glow_intensity=0, bloom_intensity=0)),
    ('realistic_emissive_high', 22, dict(moon_pattern=P.REALISTIC, moon_crescent_amount=0, moon_brightness=1.2, show_aurora=False, moon_emissive_intensity=2.5)),
    ('realistic_crescent', 22, dict(moon_pattern=P.REALISTIC, moon_crescent_amount=75, moon_phase_rotation=0, moon_brightness=1.2, show_aurora=False)),
    ('retro_overview', 80, dict(moon_pattern=P.RETRO_BANDS, moon_crescent_amount=0)),
    ('realistic_overview', 80, dict(moon_pattern=P.REALISTIC, moon_crescent_amount=0)),
]
if '-MoonPatternCleanupPreview' in unreal.SystemLibrary.get_command_line():
    out=Path(unreal.Paths.project_saved_dir())/'TrimMoonPatterns/Preview'
    out.mkdir(parents=True,exist_ok=True)
    views=[('smooth',30,dict(moon_pattern=P.SMOOTH)),
           ('retro_bands',36,dict(moon_pattern=P.RETRO_BANDS)),
           ('realistic',22,dict(moon_pattern=P.REALISTIC))]
    common=dict(moon_crescent_amount=0,moon_brightness=1.2,show_aurora=False,moon_glow_intensity=.65,
                moon_emissive_intensity=1.35,moon_texture_rotation=0,moon_retro_band_gap=1.)
    views=[(name,fov,{**common,**overrides}) for name,fov,overrides in views]
keys = set().union(*(v[2].keys() for v in views))
saved = {key: rig.get_editor_property(key) for key in keys}
state = dict(start=time.monotonic(), next=time.monotonic() + 20, index=0, task=None, capturing=False,
             prepared=False, results=[])
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    for key, value in saved.items():
        rig.set_editor_property(key, value)
    rig.apply_environment()
    actors.destroy_actor(camera)
    (out / 'render.json').write_text(json.dumps(dict(passed=error is None, error=error, views=state['results']), indent=2))
    if error:
        unreal.log_error('MOON_STYLES_RENDER_FAILED ' + str(error))
    else:
        unreal.log('MOON_STYLES_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    try:
        if state['capturing'] or time.monotonic() < state['next']:
            return
        if time.monotonic() - state['start'] > 300:
            raise RuntimeError('Moon styles capture timeout')
        if state['task']:
            if not state['task'].is_task_done():
                return
            state.update(task=None, index=state['index'] + 1, prepared=False, next=time.monotonic() + 0.5)
            if state['index'] == len(views):
                finish()
            return
        name, fov, overrides = views[state['index']]
        if not state['prepared']:
            for key, value in {**saved, **overrides}.items():
                rig.set_editor_property(key, value)
            rig.apply_environment()
            component.set_editor_property('field_of_view', float(fov))
            pitch = rig.get_editor_property('moon_elevation')
            yaw = rig.get_editor_property('moon_azimuth')
            direction = unreal.Vector(math.cos(math.radians(pitch)) * math.cos(math.radians(yaw)),
                                      math.cos(math.radians(pitch)) * math.sin(math.radians(yaw)),
                                      math.sin(math.radians(pitch)))
            position = camera.get_actor_location()
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, position + direction * 10000), False)
            unreal.SystemLibrary.execute_console_command(world, 'r.Streaming.FullyLoadUsedTextures 1')
            state.update(prepared=True, next=time.monotonic() + 4)
            return
        state['capturing'] = True
        path = out / (name + '.png')
        state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(path), camera, False, False)
        state['capturing'] = False
        assert state['task'] and state['task'].is_valid_task()
        state['results'].append(path.name)
    except Exception as error:
        finish(str(error))


state['handle'] = unreal.register_slate_post_tick_callback(tick)
