"""Check the overhead health bar in disposable PIE and capture the real UI."""
import json
import time
from pathlib import Path
import unreal

output = (Path(unreal.Paths.project_saved_dir()) / 'BlackMonster/HealthPreview').resolve()
output.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 30, .2))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,500), unreal.Rotator(pitch=-35,yaw=0,roll=0))
light.light_component.set_editor_property('intensity', 5.0)
actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,500))
post = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
post.set_editor_property('unbound', True)
settings = post.get_editor_property('settings')
settings.set_editor_property('override_auto_exposure_method', True)
settings.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
settings.set_editor_property('override_auto_exposure_bias', True)
settings.set_editor_property('auto_exposure_bias', 10.0)
post.set_editor_property('settings', settings)
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-500,0,100))
cls = unreal.load_class(None, '/Game/Characters/black-monster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C')
npc = actors.spawn_actor_from_class(cls, unreal.Vector(0,0,100), unreal.Rotator(pitch=0,yaw=180,roll=0))
npc.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
state = {'stage':'start','started':time.monotonic()}


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (output / 'result.json').write_text(json.dumps({'passed':error is None,'error':error,'clearance':state.get('clearance')}, indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    if error:
        unreal.log_error('BLACK_MONSTER_HEALTH_FAILED ' + error)
    else:
        unreal.log('BLACK_MONSTER_HEALTH_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(delta):
    try:
        now = time.monotonic()
        if now-state['started'] > 90:
            raise RuntimeError('Timed out at ' + state['stage'])
        if state['stage'] == 'start':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if world is None:
                return
            player = unreal.GameplayStatics.get_player_pawn(world, 0)
            if player is None:
                return
            player.set_actor_location(unreal.Vector(-500,0,100),False,True)
            player.get_controller().set_control_rotation(unreal.Rotator(pitch=-3,yaw=0,roll=0))
            player.character_movement.disable_movement()
            npc = unreal.GameplayStatics.get_all_actors_of_class(world, cls)[0]
            npc.character_movement.disable_movement()
            npc.mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            state.update(world=world,npc=npc,stage='capture',since=now)
        elif state['stage'] == 'capture' and now-state['since'] > 8:
            npc = state['npc']
            bar = npc.get_component_by_class(unreal.NPCHealthBarComponent)
            clearance = bar.get_world_location().z-npc.mesh.get_socket_location('Head').z
            assert clearance > 50, 'Health bar is too low'
            assert bar.is_visible(), 'Health bar is not visible to the player'
            state['clearance'] = clearance
            path = output / 'overhead_health.png'
            unreal.SystemLibrary.execute_console_command(state['world'], f'Shot SHOWUI filename="{path}" -nosuffix')
            state.update(stage='finish',since=now)
        elif state['stage'] == 'finish' and now-state['since'] > 2:
            assert (output / 'overhead_health.png').is_file(), 'Screenshot was not written'
            finish()
    except Exception as exc:
        finish(str(exc))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
