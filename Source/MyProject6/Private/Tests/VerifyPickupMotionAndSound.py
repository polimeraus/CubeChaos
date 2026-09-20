"""PIE check for six motion presets and actual collection audio, in a disposable map.

Run with -ExecutePythonScript, -RenderOffscreen, -AudioMixer and -unattended.
Do not pass -nosound: the test records the game mix to Saved/Pickups/MotionPreview.
"""
import json
import time
import wave
import array
import math
from pathlib import Path
import unreal

output = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'Pickups/MotionPreview'
output.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset('/Game/Gameplay/Pickups/BP_ParametricPickup')
assert unreal.get_default_object(bp.generated_class()).get_editor_property('motion') == unreal.PickupMotion.STATIONARY
modes = [unreal.PickupMotion.STATIONARY, unreal.PickupMotion.SPIN, unreal.PickupMotion.BOB,
         unreal.PickupMotion.SPIN_AND_BOB, unreal.PickupMotion.ORBIT, unreal.PickupMotion.ORBIT_AND_SPIN]
for i, mode in enumerate(modes):
    pickup = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector((5-i) * 180, 0, 0))
    for key, value in {'motion': mode, 'auto_collect': False, 'player_only': False,
                       'shape': unreal.PickupShape.ROUNDED_CUBE, 'motif': unreal.PickupMotif.QUESTION_MARK,
                       'bob_height': 70., 'hover_height': 25., 'pulse': False}.items():
        pickup.set_editor_property(key, value)
    pickup.set_editor_property('tags', ['PickupMotionPreview' + str(i)])
    pickup.refresh_pickup()
character = actors.spawn_actor_from_class(unreal.Character, unreal.Vector(-1000, 0, 200))
character.set_editor_property('tags', ['PickupCollectorPreview'])
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(450, 0, -7))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 20, .1))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(pitch=-55, yaw=-30))
light.light_component.set_editor_property('intensity', 5)
position = unreal.Vector(550, -950, 500)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(450, 0, 55)), False)
state = {'stage': 'world', 'started': time.monotonic()}
result = {}

def capture(name):
    state['capture'].capture_scene()
    unreal.RenderingLibrary.export_render_target(state['world'], state['target'], str(output), name + '.png')

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    result.update(passed=error is None, error=error)
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    (unreal.log_error if error else unreal.log)('PICKUP_MOTION_AUDIO_PREVIEW ' + str(result))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_):
    try:
        now = time.monotonic()
        if now - state['started'] > 160:
            raise RuntimeError('Timed out in ' + state['stage'])
        if state['stage'] == 'world':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not world:
                return
            pickups = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ParametricPickup)
            if len(pickups) != 6:
                return
            # Offscreen editors are unfocused; bypass their application mute for this test process only.
            unreal.SystemLibrary.execute_console_command(world, 'au.DisableAppVolume 1')
            pickups.sort(key=lambda p: str(p.get_editor_property('tags')[0]))
            character = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character)
                             if p.actor_has_tag('PickupCollectorPreview'))
            character.get_component_by_class(unreal.CharacterMovementComponent).disable_movement()
            component = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            component.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            component.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
            component.set_editor_property('ortho_width', 1250)
            component.set_editor_property('capture_every_frame', False)
            component.set_editor_property('capture_on_movement', False)
            post = component.get_editor_property('post_process_settings')
            for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                              'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                              'override_auto_exposure_bias': True, 'auto_exposure_bias': 1.}.items():
                post.set_editor_property(key, value)
            component.set_editor_property('post_process_settings', post)
            target = unreal.RenderingLibrary.create_render_target2d(world, 1500, 600, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property('texture_target', target)
            state.update(world=world, pickups=pickups, character=character, capture=component, target=target, stage='warmup', since=now)
        elif state['stage'] == 'warmup' and now - state['since'] > 7:
            for p in state['pickups']:
                p.refresh_motion()
            capture('motion_start')
            state['initial_positions'] = [p.get_component_by_class(unreal.BoxComponent).get_world_location() for p in state['pickups']]
            state.update(stage='moved', since=now)
        elif state['stage'] == 'moved' and now - state['since'] > .7:
            capture('motion_later')
            distances = [(p.get_component_by_class(unreal.BoxComponent).get_world_location() - start).length()
                         for p, start in zip(state['pickups'], state['initial_positions'])]
            assert distances[0] < .01 and distances[1] < .01, 'Stationary/spinning centre drift'
            assert all(value > 1 for value in distances[2:]), 'A translation animation did not move'
            result['centre_travel_cm'] = distances
            sound = state['pickups'][3].get_editor_property('collection_sound')
            assert sound and sound.duration > .3
            result['sound'] = sound.get_path_name()
            result['sound_duration'] = sound.duration
            unreal.AudioMixerLibrary.start_recording_output(state['world'], 5.)
            state.update(stage='collect', since=now)
        elif state['stage'] == 'collect' and now - state['since'] > .4:
            pickup = state['pickups'][3]
            pickup.set_editor_property('sound_volume', .55)
            pickup.set_editor_property('sound_pitch', 1.1)
            pickup.set_editor_property('auto_collect', True)
            location = pickup.get_component_by_class(unreal.BoxComponent).get_world_location()
            state['character'].set_actor_location(location, False, True)
            assert pickup.is_collected(), 'Touching moving pickup did not collect'
            assert not pickup.try_collect(state['character']), 'Duplicate collection accepted'
            voices = [c for c in state['world'].get_world_settings().get_components_by_class(unreal.AudioComponent)
                      if c.get_editor_property('sound') == pickup.get_editor_property('collection_sound')]
            assert len(voices) == 1, 'Collection must create exactly one voice'
            result['audio_voice_count'] = len(voices)
            result['audio_volume'] = voices[0].get_editor_property('volume_multiplier')
            result['audio_pitch'] = voices[0].get_editor_property('pitch_multiplier')
            state['voice'] = voices[0]
            result['collected_at_recording_seconds'] = now - state['since']
            state.update(stage='tail', since=now)
        elif state['stage'] == 'tail' and now - state['since'] > 3.2:
            remaining = unreal.GameplayStatics.get_all_actors_of_class(state['world'], unreal.ParametricPickup)
            assert len(remaining) == 5, 'Collected actor was not destroyed'
            unreal.AudioMixerLibrary.stop_recording_output(state['world'], unreal.AudioRecordingExportType.WAV_FILE,
                                                                   'collected_in_game', str(output))
            state.update(stage='export', since=now)
        elif state['stage'] == 'export' and now - state['since'] > 1:
            assert (output / 'collected_in_game.wav').exists(), 'No recorded game audio'
            with wave.open(str(output / 'collected_in_game.wav'), 'rb') as recorded:
                rate, channels = recorded.getframerate(), recorded.getnchannels()
                assert recorded.getsampwidth() == 2
                data = array.array('h', recorded.readframes(recorded.getnframes()))
            result['recorded_peak'] = max(abs(sample) for sample in data) / 32768
            tail = data[int(.9 * rate) * channels:int(1.5 * rate) * channels]
            result['rms_after_actor_destroyed'] = math.sqrt(sum(sample*sample for sample in tail)/max(1,len(tail))) / 32768
            assert result['recorded_peak'] > .001, 'Recorded sound is silent'
            assert result['rms_after_actor_destroyed'] > .0001, 'Sound did not continue after actor destruction'
            finish()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
