"""Render the saved spawn-point Blueprint in disposable PIE; never save a level.

Run UnrealEditor-Cmd with -ExecutePythonScript=<this file> (with a real RHI).
"""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'NPCSpawnPoint/VisualPreview'
output.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 30, 0.2))
for rot, intensity in [(unreal.Rotator(pitch=-50, yaw=-30, roll=0), 2.0), (unreal.Rotator(pitch=-25, yaw=150, roll=0), 0.6)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600), rot)
    light.light_component.set_editor_property('intensity', intensity)

point_class = unreal.load_asset('/Game/NPC/Spawning/BP_NPCSpawnPoint').generated_class()
point = actors.spawn_actor_from_class(point_class, unreal.Vector(0, 0, 100))
point.set_editor_property('spawn_point_id', 'Preview')
point.set_editor_property('spawn_on_begin_play', False)
point.set_editor_property('default_npc_class', unreal.Character)
spawn_settings = point.get_editor_property('spawn_settings')
spawn_settings.set_editor_property('spawn_radius', 0.0)
point.set_editor_property('spawn_settings', spawn_settings)

camera_position = unreal.Vector(410, -550, 330)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, camera_position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera_position, unreal.Vector(0, 0, 65)), False)
state = {'stage': 'world', 'started': time.monotonic(), 'done': False}
result = {}


def finish(error=None):
    state['done'] = True
    unreal.unregister_slate_post_tick_callback(state['handle'])
    result.update(passed=error is None, error=error)
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    if error:
        unreal.log_error('NPC_PORTAL_RENDER_FAILED ' + str(error))
    else:
        unreal.log('NPC_PORTAL_RENDER_OK')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def capture(name):
    state['capture'].capture_scene()
    unreal.RenderingLibrary.export_render_target(state['world'], state['target'], str(output), name + '.png')


def tick(_delta):
    if state['done']:
        return
    try:
        now = time.monotonic()
        if now - state['started'] > 150:
            raise RuntimeError('Timed out in ' + state['stage'])
        if state['stage'] == 'world':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not world:
                return
            points = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.NPCSpawnPoint)
            cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if not points or not cameras:
                return
            component = cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            component.set_editor_property('capture_every_frame', True)
            component.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            component.set_editor_property('fov_angle', 48.0)
            post = component.get_editor_property('post_process_settings')
            post.set_editor_property('override_auto_exposure_method', True)
            post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
            post.set_editor_property('override_auto_exposure_apply_physical_camera_exposure', True)
            post.set_editor_property('auto_exposure_apply_physical_camera_exposure', False)
            post.set_editor_property('override_auto_exposure_bias', True)
            post.set_editor_property('auto_exposure_bias', 0.0)
            component.set_editor_property('post_process_settings', post)
            target = unreal.RenderingLibrary.create_render_target2d(world, 800, 800, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property('texture_target', target)
            state.update(world=world, point=points[0], capture=component, target=target, stage='warmup', since=now)
            unreal.log('NPC_PORTAL_PYTHON_METHODS ' + str([name for name in dir(points[0]) if 'spawn' in name]))
        elif state['stage'] == 'warmup' and now - state['since'] > 8:
            portal_layers = state['point'].get_editor_property('portal_layers')
            if len(portal_layers) != 3:
                raise RuntimeError('Saved Blueprint did not acquire three native portal layers')
            for layer in portal_layers:
                if layer.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION:
                    raise RuntimeError('Portal visual unexpectedly blocks spawning')
                if not isinstance(layer.get_material(0), unreal.MaterialInstanceDynamic):
                    raise RuntimeError('Missing portal material instance')
            result['portal_location'] = str(state['point'].get_editor_property('portal_root').get_world_location())
            result['actor_tick_enabled'] = state['point'].is_actor_tick_enabled()
            if result['actor_tick_enabled']:
                raise RuntimeError('Idle visual added an actor tick')
            capture('portal_idle')
            state.update(stage='animated', since=now)
        elif state['stage'] == 'animated' and now - state['since'] > 0.55:
            capture('portal_animated')
            state.update(stage='trigger')
        elif state['stage'] == 'trigger':
            state['point'].spawn_default_np_cs()
            state.update(stage='pulse', since=now)
        elif state['stage'] == 'pulse' and state['point'].get_spawned_np_cs():
            layers = state['point'].get_editor_property('portal_layers')
            result['pulse_time'] = layers[0].get_material(0).get_scalar_parameter_value('SpawnTime')
            if result['pulse_time'] < 0:
                raise RuntimeError('Successful spawn did not trigger the pulse')
            capture('portal_spawn_pulse')
            # Remove the test character so the recovered image shows the same empty portal.
            for npc in state['point'].get_spawned_np_cs():
                npc.destroy_actor()
            state.update(stage='settle', since=now)
        elif state['stage'] == 'settle' and now - state['since'] > 1.0:
            capture('portal_recovered')
            visual = state['point'].get_editor_property('visual_settings')
            visual.set_editor_property('enabled', False)
            state['point'].set_editor_property('visual_settings', visual)
            state['point'].refresh_portal_visuals()
            capture('portal_disabled')
            finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
