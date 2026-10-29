"""Render the saved platform in a fresh real-RHI process, including alternate shapes."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'LobbyPlatform/Preview'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/GameplayActors/LobbyPlatform/Maps/Lvl_LobbyPlatformPreview')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lobby = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricLobbyPlatform))
camera = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.CameraActor))
component = camera.camera_component
component.set_editor_property('constrain_aspect_ratio', False)
post_actor = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.PostProcessVolume))
component.set_editor_property('post_process_settings', post_actor.get_editor_property('settings'))
component.set_editor_property('post_process_blend_weight', 1.)
actors.set_selected_level_actors([])
for cmd in ('ShowFlag.PostProcessing 1', 'ShowFlag.Bloom 1', 'ShowFlag.Tonemapper 1',
            'r.BloomQuality 5', 'r.AntiAliasingMethod 1', 'viewmode lit'):
    unreal.SystemLibrary.execute_console_command(world, cmd)
views = ['Reference', 'Top', 'CircleEnds', 'EdgeEnds', 'Triangle', 'Ellipse', 'Damage']
if '-LobbyDetail' in unreal.SystemLibrary.get_command_line():
    views = ['DamageClose']
state = {'next': time.monotonic() + 20, 'start': time.monotonic(), 'index': 0, 'prepared': False, 'task': None}


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out / 'Render.json').write_text(json.dumps({'passed': error is None, 'error': error, 'views': views}, indent=2))
    if error:
        unreal.log_error('LOBBY_RENDER_FAILED ' + error)
    else:
        unreal.log('LOBBY_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_):
    if time.monotonic() < state['next']:
        return
    try:
        assert time.monotonic() - state['start'] < 360, 'Render timed out'
        if state['task']:
            if not state['task'].is_task_done():
                return
            state.update(task=None, index=state['index']+1, prepared=False)
            if state['index'] == len(views):
                finish()
                return
        name = views[state['index']]
        if not state['prepared']:
            lobby.repair_platform()
            lobby.reset_reference_layout()
            if name in ('CircleEnds', 'EdgeEnds'):
                pads = list(lobby.pads)
                for pad in pads[1:]:
                    pad.set_editor_property('show_circle', name == 'CircleEnds')
                lobby.set_editor_property('pads', pads)
            lobby.set_editor_property('shape', unreal.LobbyPlatformShape.TRIANGLE if name == 'Triangle' else
                                      unreal.LobbyPlatformShape.ELLIPSE if name == 'Ellipse' else
                                      unreal.LobbyPlatformShape.ROUNDED_RECTANGLE)
            lobby.rebuild_platform()
            if name in ('Damage', 'DamageClose'):
                assert lobby.remove_platform_sphere(unreal.Vector(0, -700, 0), 380)
                assert lobby.remove_platform_sphere(unreal.Vector(-1500, -500, -20), 350)
            position = unreal.Vector(0, -100, 7800) if name == 'Top' else unreal.Vector(1500, -6500, 5200)
            look = unreal.Vector(0,0,-30)
            if name == 'DamageClose':
                position, look = unreal.Vector(200, -1650, 650), unreal.Vector(0,-700,-40)
                material = lobby.get_dynamic_mesh_component().get_material(4)
                unreal.log('LOBBY_INTERIOR ' + material.get_editor_property('parent').get_path_name() + ' ' + str({
                    key: material.get_scalar_parameter_value(key) for key in ('NeonIntensity','LayerCenter','LayerHalfWidth')}))
            camera.set_actor_location(position, False, False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, look), False)
            component.set_editor_property('field_of_view', 40.)
            state.update(prepared=True, next=time.monotonic()+4)
            return
        state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1440, 1100, str(out/(name+'.png')), camera, False, False)
        assert state['task'] and state['task'].is_valid_task()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
