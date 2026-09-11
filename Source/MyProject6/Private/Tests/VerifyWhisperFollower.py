"""Verify the saved Whisper actor follows, stops, and resumes in disposable PIE.

Run with -ExecutePythonScript in an editor process dedicated to verification.
"""
import json
import time
from pathlib import Path
import unreal

state = {'stage': 'world', 'deadline': time.monotonic() + 100, 'samples': []}
output = Path(unreal.Paths.project_saved_dir()) / 'WhisperSetup/verification.json'


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    output.write_text(json.dumps({'passed': error is None, 'error': error, 'samples': state['samples']}, indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    if error:
        unreal.log_error('WHISPER_VERIFY_FAILED ' + error)
    else:
        unreal.log('WHISPER_VERIFY_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def distance(a, b):
    v = a.get_actor_location() - b.get_actor_location()
    return (v.x * v.x + v.y * v.y) ** .5


def tick(delta):
    try:
        now = time.monotonic()
        if now > state['deadline']:
            raise RuntimeError('Timeout at ' + state['stage'])
        if state['stage'] == 'world':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if world is None:
                return
            npc_list = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WhisperFollower)
            player = unreal.GameplayStatics.get_player_pawn(world, 0)
            if player is None or not npc_list:
                return
            assert len(npc_list) == 1, 'Expected exactly one saved Whisper actor'
            npc = npc_list[0]
            assert isinstance(npc.get_controller(), unreal.ChasingAIController), 'AI possession failed'
            assert npc.mesh.get_skinned_asset().get_name() == 'SK_Whisper', 'Whisper mesh was not saved'
            assert isinstance(npc.mesh.get_anim_instance(), unreal.AnimSingleNodeInstance), 'Locomotion did not initialize'
            assert npc.mesh.get_anim_instance().get_animation_asset().get_name() == 'BS_Whisper_Locomotion'
            npc.mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            # Isolate this follower from the existing enemies during the test only.
            for enemy in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ChasingNPC):
                enemy.destroy_actor()
            state['samples'].append({'player_class': player.get_class().get_name()})
            state.update(world=world, npc=npc, player=player, initial=distance(npc, player), since=now, stage='approach')
            return
        npc, player = state['npc'], state['player']
        dist = distance(npc, player)
        speed = npc.get_velocity().length()
        state['samples'].append({'stage': state['stage'], 'distance': round(dist, 2), 'speed': round(speed, 2),
                                 'npc': str(npc.get_actor_location()), 'player': str(player.get_actor_location())})
        if state['stage'] == 'approach' and now - state['since'] > 2.0:
            assert dist < state['initial'] - 100, 'Whisper did not approach the player'
            state.update(stage='stop', since=now)
        elif state['stage'] == 'stop' and now - state['since'] > 5.0:
            assert dist < 210, 'Whisper did not reach the follow distance'
            assert speed < 10, 'Whisper did not stop beside the player'
            head = npc.mesh.get_socket_location('head')
            feet_z = max(npc.mesh.get_socket_location('foot_l').z, npc.mesh.get_socket_location('foot_r').z)
            assert head.z - feet_z > 120, 'Animated Whisper pose is not upright'
            target = player.get_actor_location() + unreal.Vector(250, 0, 0)
            player.set_actor_location(target, False, True)
            state.update(stage='resume', since=now, initial=distance(npc, player))
        elif state['stage'] == 'resume' and now - state['since'] > 1.5:
            assert dist < state['initial'] - 100, 'Whisper did not resume following after the player moved'
            finish()
    except Exception as exc:
        finish(str(exc))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
