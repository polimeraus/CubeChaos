"""Actual viewport previews of all moon styles/phases. Test overrides are never saved."""
import json
import math
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightMoon'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'Lvl_FirstPerson'
for name in ('PostProcessing', 'Bloom', 'Tonemapper'):
    unreal.SystemLibrary.execute_console_command(world, 'ShowFlag.' + name + ' 1')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rig = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment))
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 355), transient=True)
camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view', 60.0)
elevation = math.radians(rig.get_editor_property('moon_elevation') - 8)
azimuth = math.radians(rig.get_editor_property('moon_azimuth'))
position = camera.get_actor_location()
target = position + unreal.Vector(math.cos(elevation) * math.cos(azimuth),
                                  math.cos(elevation) * math.sin(azimuth), math.sin(elevation)) * 10000
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, target), False)
P = unreal.RetroMoonPattern
C = unreal.RetroMoonColor
views = [('default', P.REALISTIC, C.PALE_YELLOW, 0, 10, 0),
         ('smooth', P.SMOOTH, C.WHITE, 0, 10, 0),
         ('retro_bands', P.RETRO_BANDS, C.PALE_ORANGE, 0, 10, 0),
         ('half', P.REALISTIC, C.WHITE, 50, 10, 0),
         ('crescent', P.REALISTIC, C.PALE_ORANGE, 75, 10, 0),
         ('thin_crescent', P.REALISTIC, C.PALE_YELLOW, 90, 10, 180),
         ('new_moon', P.REALISTIC, C.PALE_YELLOW, 100, 10, 0),
         ('original_size', P.REALISTIC, C.PALE_YELLOW, 0, 4, 0)]
state = dict(start=time.monotonic(), next=time.monotonic()+15, index=0, task=None,
             capturing=False, prepared=False, results=[])
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out / 'render.json').write_text(json.dumps(dict(passed=error is None, error=error, views=state['results']), indent=2))
    if error:
        unreal.log_error('MOON_RENDER_FAILED ' + str(error))
    else:
        unreal.log('MOON_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    try:
        if state['capturing']:
            return
        if time.monotonic() - state['start'] > 240:
            raise RuntimeError('Moon capture timeout')
        if time.monotonic() < state['next']:
            return
        if state['task']:
            if not state['task'].is_task_done():
                return
            state.update(task=None, index=state['index']+1, prepared=False, next=time.monotonic()+1)
            if state['index'] == len(views):
                finish()
            return
        name, pattern, color, phase, diameter, rotation = views[state['index']]
        if not state['prepared']:
            for key, value in dict(moon_pattern=pattern, moon_color_preset=color, moon_crescent_amount=phase,
                                   moon_diameter=diameter, moon_phase_rotation=rotation).items():
                rig.set_editor_property(key, value)
            rig.apply_environment()
            state.update(prepared=True, next=time.monotonic()+2)
            return
        path = out / ('moon_' + name + '.png')
        state['capturing'] = True  # Screenshot loading can pump nested Slate callbacks.
        state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, str(path), camera, False, False)
        state['capturing'] = False
        assert state['task'] and state['task'].is_valid_task()
        state['results'].append(path.name)
    except Exception as error:
        finish(str(error))
state['handle'] = unreal.register_slate_post_tick_callback(tick)
