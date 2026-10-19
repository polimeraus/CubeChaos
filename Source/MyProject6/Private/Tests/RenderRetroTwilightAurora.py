"""Render the saved rig and unsaved control comparisons in a separate editor process."""
import json
import math
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightAurora'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'Lvl_FirstPerson'
for name in ('PostProcessing', 'Bloom', 'Tonemapper'):
    unreal.SystemLibrary.execute_console_command(world, 'ShowFlag.' + name + ' 1')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rig = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'RetroTwilightEnvironment')
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 355), transient=True)
component = camera.get_component_by_class(unreal.CameraComponent)
keys = list(json.loads((out / 'upgrade.json').read_text())['settings']) + ['moon_crescent_amount', 'moon_diameter']
saved = {key: rig.get_editor_property(key) for key in keys}
views = [
    ('overview', 80, 28, {}, 3),
    ('moon_detail', 28, None, {'aurora_flow_speed': 0}, 2),
    ('moon_full', 28, None, {'show_aurora': False, 'moon_crescent_amount': 0}, 2),
    ('aurora_frozen_a', 85, 39, {'aurora_flow_speed': 0}, 2),
    ('aurora_frozen_b', 85, 39, {'aurora_flow_speed': 0}, 4),
    ('aurora_off', 85, 39, {'show_aurora': False}, 2),
    ('aurora_wide', 85, 39, {'aurora_flow_speed': 0, 'aurora_width': 22.0, 'aurora_density': 1.3}, 2),
    ('aurora_palette', 85, 39, {'aurora_flow_speed': 0,
        'aurora_lower_color': unreal.LinearColor(0.8, 0.05, 0.3, 1),
        'aurora_upper_color': unreal.LinearColor(1, 0.35, 0.03, 1)}, 2),
    ('aurora_motion_a', 85, 39, {'aurora_flow_speed': 0.3}, 2),
    ('aurora_motion_b', 85, 39, {'aurora_flow_speed': 0.3}, 7),
    ('moon_glow_off', 28, None, {'show_aurora': False, 'moon_glow_intensity': 0}, 2),
    ('moon_glow_on', 28, None, {'show_aurora': False}, 2),
    ('moon_crescent', 28, None, {'show_aurora': False, 'moon_crescent_amount': 75}, 2),
]
state = dict(start=time.monotonic(), next=time.monotonic() + 20, index=0, task=None,
             capturing=False, prepared=False, results=[])
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out / 'render.json').write_text(json.dumps(dict(passed=error is None, error=error, views=state['results']), indent=2))
    if error:
        unreal.log_error('AURORA_RENDER_FAILED ' + str(error))
    else:
        unreal.log('AURORA_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    try:
        if state['capturing']:
            return
        if time.monotonic() - state['start'] > 300:
            raise RuntimeError('Aurora capture timeout')
        if time.monotonic() < state['next']:
            return
        if state['task']:
            if not state['task'].is_task_done():
                return
            state.update(task=None, index=state['index'] + 1, prepared=False, next=time.monotonic() + 0.5)
            if state['index'] == len(views):
                finish()
            return
        name, fov, elevation, overrides, wait = views[state['index']]
        if not state['prepared']:
            for key, value in {**saved, **overrides}.items():
                rig.set_editor_property(key, value)
            rig.apply_environment()
            component.set_editor_property('field_of_view', float(fov))
            pitch = rig.get_editor_property('moon_elevation') if elevation is None else elevation
            yaw = rig.get_editor_property('moon_azimuth')
            direction = unreal.Vector(math.cos(math.radians(pitch)) * math.cos(math.radians(yaw)),
                                      math.cos(math.radians(pitch)) * math.sin(math.radians(yaw)),
                                      math.sin(math.radians(pitch)))
            position = camera.get_actor_location()
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, position + direction * 10000), False)
            state.update(prepared=True, next=time.monotonic() + wait)
            return
        path = out / (name + '.png')
        state['capturing'] = True
        state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(path), camera, False, False)
        state['capturing'] = False
        assert state['task'] and state['task'].is_valid_task()
        state['results'].append(path.name)
    except Exception as error:
        finish(str(error))


state['handle'] = unreal.register_slate_post_tick_callback(tick)
