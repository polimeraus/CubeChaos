"""Render a disposable PIE launch pad and check automatic contact activation."""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'LaunchPad/Preview'
output.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 30, .2))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-50, yaw=-30))
light.light_component.set_editor_property('intensity', 3)
pad = actors.spawn_actor_from_class(unreal.load_asset('/Game/Gameplay/LaunchPad/BP_LaunchPad').generated_class(), unreal.Vector())
test_character = actors.spawn_actor_from_class(unreal.Character, unreal.Vector(400, 0, 120))
test_character.set_editor_property('tags', ['LaunchPadPreviewCharacter'])
position = unreal.Vector(260, -320, 350)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 5)), False)
state = {'stage': 'world', 'started': time.monotonic(), 'done': False}
result = {}

def finish(error=None):
    state['done'] = True
    unreal.unregister_slate_post_tick_callback(state['handle'])
    result.update(passed=error is None, error=error)
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    (unreal.log_error if error else unreal.log)('LAUNCH_PAD_PREVIEW ' + str(result))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def capture(name):
    state['capture'].capture_scene()
    unreal.RenderingLibrary.export_render_target(state['world'], state['target'], str(output), name + '.png')

def tick(_):
    try:
        now = time.monotonic()
        if now - state['started'] > 150:
            raise RuntimeError('Preview timed out in ' + state['stage'])
        if state['stage'] == 'world':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not world:
                return
            pads = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LaunchPad)
            cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if not pads or not cameras:
                return
            component = cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            component.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            component.set_editor_property('fov_angle', 40)
            post = component.get_editor_property('post_process_settings')
            post.set_editor_property('override_auto_exposure_method', True)
            post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
            post.set_editor_property('override_auto_exposure_apply_physical_camera_exposure', True)
            post.set_editor_property('auto_exposure_apply_physical_camera_exposure', False)
            post.set_editor_property('override_auto_exposure_bias', True)
            post.set_editor_property('auto_exposure_bias', 0)
            component.set_editor_property('post_process_settings', post)
            target = unreal.RenderingLibrary.create_render_target2d(world, 800, 800, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property('texture_target', target)
            state.update(world=world, pad=pads[0], capture=component, target=target, stage='warmup', since=now)
        elif state['stage'] == 'warmup' and now - state['since'] > 6:
            material = state['pad'].get_editor_property('symbol').get_material(0)
            assert isinstance(material, unreal.MaterialInstanceDynamic), 'Blueprint material missing'
            assert state['pad'].get_editor_property('shape') == unreal.LaunchPadShape.CIRCLE
            assert state['pad'].get_editor_property('pattern') == unreal.LaunchPadPattern.CONVERGING_RINGS
            assert state['pad'].get_editor_property('launch_angle') == 90
            capture('idle')
            state.update(stage='animated', since=now)
        elif state['stage'] == 'animated' and now - state['since'] > .5:
            capture('animated')
            character = next(actor for actor in unreal.GameplayStatics.get_all_actors_of_class(state['world'], unreal.Character)
                             if actor.actor_has_tag('LaunchPadPreviewCharacter'))
            character.get_component_by_class(unreal.CharacterMovementComponent).set_editor_property('run_physics_with_no_controller', True)
            half = character.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
            character.set_actor_location(unreal.Vector(0, 0, half + 10), False, True)
            state.update(character=character, stage='launch', since=now)
        elif state['stage'] == 'launch':
            velocity = state['character'].get_velocity()
            if velocity.z > 800:
                result['launch_vertical_velocity'] = velocity.z
                result['pulse_time'] = state['pad'].get_editor_property('symbol').get_material(0).get_scalar_parameter_value('SpawnTime')
                assert result['pulse_time'] >= 0, 'No launch pulse'
                capture('launch_pulse')
                state.update(stage='flight', since=now)
            elif now - state['since'] > 3:
                raise RuntimeError('Contact did not launch character')
        elif state['stage'] == 'flight' and now - state['since'] > .3:
            result['character_height'] = state['character'].get_actor_location().z
            assert result['character_height'] > 200, 'Character did not rise'
            state['character'].destroy_actor()
            state['pad'].set_editor_property('shape', unreal.LaunchPadShape.SQUARE)
            state['pad'].refresh_pad()
            state.update(stage='square_rings', since=now)
        elif state['stage'] == 'square_rings' and now - state['since'] > .3:
            capture('square_rings')
            state['pad'].set_editor_property('pattern', unreal.LaunchPadPattern.ARROWS)
            state['pad'].refresh_pad()
            state.update(stage='square_arrows', since=now)
        elif state['stage'] == 'square_arrows' and now - state['since'] > 8:
            capture('square_arrows')
            state['pad'].set_editor_property('shape', unreal.LaunchPadShape.CIRCLE)
            state['pad'].refresh_pad()
            state.update(stage='circle_arrows', since=now)
        elif state['stage'] == 'circle_arrows' and now - state['since'] > 3:
            capture('circle_arrows')
            finish()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
