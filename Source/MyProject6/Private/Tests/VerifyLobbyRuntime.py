"""Disposable PIE verifies BeginPlay spawning, attachment and actor teardown."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'LobbyPlatform'
unreal.EditorLoadingAndSavingUtils.load_map('/Game/GameplayActors/LobbyPlatform/Maps/Lvl_LobbyPlatformPreview')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lobby = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricLobbyPlatform))
pads = list(lobby.pads)
for pad in pads:
    pad.set_editor_property('show_circle', True)
    pad.set_editor_property('actor_class', unreal.CameraActor)
    pad.set_editor_property('spawn_offset', unreal.Vector(0, 0, 100))
lobby.set_editor_property('pads', pads)
lobby.rebuild_platform()
assert all(lobby.get_spawned_actor_at_pad(p.id) is None for p in pads)
state = {'start': time.monotonic()}


def close(a, b):
    return all(abs(getattr(a, axis) - getattr(b, axis)) < .01 for axis in ('x', 'y', 'z'))


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    result = {'passed': error is None, 'error': error, 'begin_play_spawns': 5,
              'named_transform_matches': error is None, 'repeat_calls_do_not_duplicate': error is None,
              'attachment_follows_platform': error is None, 'teardown_cleans_children': error is None}
    (out / 'Runtime.json').write_text(json.dumps(result, indent=2))
    if error:
        unreal.log_error('LOBBY_RUNTIME_FAILED ' + error)
    else:
        unreal.log('LOBBY_RUNTIME_OK')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_):
    try:
        assert time.monotonic() - state['start'] < 90, 'PIE timeout'
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            return
        platforms = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.ParametricLobbyPlatform)
        if not platforms or time.monotonic() - state['start'] < 3:
            return
        platform = platforms[0]
        spawned = [platform.get_spawned_actor_at_pad(p.id) for p in pads]
        assert len(spawned) == 5 and all(spawned), 'BeginPlay did not spawn every configured class'
        assert not platform.is_actor_tick_enabled()
        for pad, actor in zip(pads, spawned):
            expected = platform.get_pad_spawn_transform(pad.id)
            assert expected, str(pad.id)
            assert close(actor.get_actor_location(), expected.translation), str(pad.id)
            assert actor.get_attach_parent_actor() == platform
            assert platform.spawn_actor_at_pad(pad.id, unreal.CameraActor) == actor
        assert len(platform.spawn_configured_actors()) == 5
        old_positions = [a.get_actor_location() for a in spawned]
        delta = unreal.Vector(400, 250, 120)
        platform.set_actor_location(delta, False, False)
        for actor, old in zip(spawned, old_positions):
            assert close(actor.get_actor_location(), old + delta)
        platform.rebuild_platform()
        assert all(platform.get_spawned_actor_at_pad(p.id) == a for p, a in zip(pads, spawned))
        names = {a.get_name() for a in spawned}
        platform.destroy_actor()
        remaining = {a.get_name() for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.CameraActor)}
        assert not names.intersection(remaining), 'Owned spawns survived platform destruction'
        finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
