"""Render the saved main map in a real RHI editor, without saving preview cameras or overrides."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'TwilightEnvironment'
out.mkdir(parents=True, exist_ok=True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not world or world.get_name() != 'Lvl_FirstPerson':
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rigs = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RetroTwilightEnvironment)]
prefix = 'after' if rigs else 'before'
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(), transient=True)
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('fov_angle', 80.0)
capture.set_editor_property('post_process_blend_weight', 0.0)
target = unreal.RenderingLibrary.create_render_target2d(world, 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
views = [('street', (-1000, -3000, 650), (-500, 3000, 900)),
         ('overview', (-11500, -16000, 6500), (500, 2000, -300)),
         ('spawn', (0, 0, 355), (-4000, 0, 400))]
state = dict(start=time.monotonic(), since=time.monotonic(), index=0, stage=0, frames=0, results=[])
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out / (prefix + '_render.json')).write_text(json.dumps(dict(passed=error is None, error=error, views=state['results']), indent=2))
    actors.destroy_actor(camera)
    if error:
        unreal.log_error('TWILIGHT_RENDER_FAILED ' + str(error))
    else:
        unreal.log('TWILIGHT_RENDER_OK ' + prefix)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_delta):
    try:
        now = time.monotonic()
        if now - state['start'] > 240:
            raise RuntimeError('Render timeout')
        if now - state['start'] < 15:
            return
        name, loc, look = views[state['index']]
        if state['stage'] == 0:
            pos, point = unreal.Vector(*loc), unreal.Vector(*look)
            camera.set_actor_location(pos, False, False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos, point), False)
            state.update(stage=1, since=now, frames=0)
        elif state['stage'] == 1:
            capture.capture_scene()
            state['frames'] += 1
            if state['frames'] > 20 and now - state['since'] > 3:
                state['stage'] = 2
        else:
            filename = prefix + '_' + name + '.png'
            unreal.RenderingLibrary.export_render_target(world, target, str(out), filename)
            state['results'].append(filename)
            state.update(index=state['index'] + 1, stage=0)
            if state['index'] == len(views):
                finish()
    except Exception as error:
        finish(str(error))
state['handle'] = unreal.register_slate_post_tick_callback(tick)
