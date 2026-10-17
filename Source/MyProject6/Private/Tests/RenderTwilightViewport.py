"""Capture the saved level plus unsaved bloom comparisons; no assets are written."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightEnvironment'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'Lvl_FirstPerson'
unreal.SystemLibrary.execute_console_command(world, 'ShowFlag.PostProcessing 1')
unreal.SystemLibrary.execute_console_command(world, 'ShowFlag.Bloom 1')
unreal.SystemLibrary.execute_console_command(world, 'ShowFlag.Tonemapper 1')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rig = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment))
dome = rig.get_editor_property('sky_dome')
unreal.log('TWILIGHT_DOME ' + str(dome.get_world_scale()) + ' material=' + str(dome.get_material(0)) +
           ' bounds=' + str(dome.get_local_bounds()) + ' visible=' + str(dome.is_visible()))
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(), transient=True)
camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view', 80.0)
views = [('street', (-1000, -3000, 650), (-500, 3000, 900)),
         ('overview', (-11500, -16000, 6500), (500, 2000, -300)),
         ('moon', (0, 0, 355), (-5500, 3900, 3200)),
         ('bloom_off', (-1000, -3000, 650), (-500, 3000, 900)),
         ('bloom_strong', (-1000, -3000, 650), (-500, 3000, 900)),
         ('camera_post', (-1000, -3000, 650), (-500, 3000, 900))]
state = dict(start=time.monotonic(), next=time.monotonic()+15, index=0, task=None, capturing=False, results=[])
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out/'viewport_render.json').write_text(json.dumps(dict(passed=error is None, error=error, views=state['results']), indent=2))
    if error:
        unreal.log_error('TWILIGHT_VIEWPORT_FAILED ' + str(error))
    else:
        unreal.log('TWILIGHT_VIEWPORT_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(_delta):
    try:
        if state['capturing']:
            return
        if time.monotonic() - state['start'] > 180:
            raise RuntimeError('Viewport capture timeout')
        if time.monotonic() < state['next']:
            return
        if state['task']:
            if not state['task'].is_task_done():
                return
            state.update(task=None, index=state['index']+1, next=time.monotonic()+3)
            if state['index'] == len(views):
                finish()
            return
        name, loc, look = views[state['index']]
        if name in ('bloom_off', 'bloom_strong', 'camera_post'):
            rig.set_editor_property('bloom_intensity', 0.0 if name == 'bloom_off' else 3.0)
            rig.apply_environment()
            if name == 'camera_post':
                camera.get_component_by_class(unreal.CameraComponent).set_editor_property(
                    'post_process_settings', rig.get_editor_property('neon_post_process').get_editor_property('settings'))
        pos = unreal.Vector(*loc)
        camera.set_actor_location(pos, False, False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos, unreal.Vector(*look)), False)
        path = out / ('viewport_' + name + '.png')
        state['capturing'] = True
        state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, str(path), camera, False, False)
        state['capturing'] = False
        assert state['task'] and state['task'].is_valid_task()
        state['results'].append(str(path.name))
    except Exception as error:
        finish(str(error))
state['handle'] = unreal.register_slate_post_tick_callback(tick)
