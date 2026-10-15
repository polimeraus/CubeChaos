"""Verify saved settings and Clown gameplay in a disposable PIE world. No saves."""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = (Path(unreal.Paths.project_saved_dir()) / 'ClownGhoul').resolve()
ROOT = '/Game/NPC/ClownMonster'
state = {'stage': 'begin', 'since': time.monotonic(), 'started': time.monotonic(), 'checks': []}


def check(condition, message):
    assert condition, message
    state['checks'].append(message)


cls = unreal.load_class(None, ROOT + '/BP_ClownMonster_NPC.BP_ClownMonster_NPC_C')
spawn_cls = unreal.load_class(None, '/Game/GameplayActors/NPCSpawner/BP_NPCSpawnPoint.BP_NPCSpawnPoint_C')
check(cls is not None, 'Clown NPC Blueprint loads')
check(unreal.get_default_object(spawn_cls).get_editor_property('npc_types').get('ClownMonster') == cls,
      'Reusable spawn point contains ClownMonster alias')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
placed = actors.get_all_level_actors()
ghoul = next(a for a in placed if a.get_actor_label() == 'NPC_PlayerChaser')
reaction = ghoul.get_component_by_class(unreal.NPCHitReactionComponent)
check(reaction.get_editor_property('forward_bend_angle') == 65, 'Saved NPC_PlayerChaser bend is 65 degrees')
check(ghoul.get_editor_property('death_animation').get_name() == 'A_Ghoul_Death_FullFall', 'Saved NPC_PlayerChaser has full-fall animation')
for point in [a for a in placed if isinstance(a, unreal.NPCSpawnPoint)]:
    check(point.get_editor_property('npc_types').get('ClownMonster') == cls, 'Placed spawn point contains ClownMonster alias')

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None,
    '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40, 40, .2))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(750, 0, 100), unreal.Rotator(pitch=0, yaw=180, roll=0))
npc = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, 100))
point = actors.spawn_actor_from_class(spawn_cls, unreal.Vector(-800, 800, 0))
point.set_editor_property('spawn_point_id', 'ClownVerification')
for index, (shot_class, tag) in enumerate([(unreal.BallProjectile, 'YellowTest'), (unreal.CannonballProjectile, 'RedTest')]):
    shot = actors.spawn_actor_from_class(shot_class, unreal.Vector(-1800, -1800+index*300, 500))
    shot.set_actor_enable_collision(False)
    shot.set_editor_property('tags', [unreal.Name(tag)])
    shot.set_editor_property('initial_life_span', 0.0)
    shot.get_component_by_class(unreal.ProjectileMovementComponent).set_auto_activate(False)
unreal.SystemLibrary.execute_console_command(world, 't.MaxFPS 60')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (OUT / 'GameplayVerification.json').write_text(json.dumps({'passed': error is None, 'error': error,
        'checks': state['checks']}, indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.execute_console_command(world, 'QUIT_EDITOR')


def shoot(projectile_class, pawn):
    tag = 'RedTest' if projectile_class == unreal.CannonballProjectile else 'YellowTest'
    shot = unreal.GameplayStatics.get_all_actors_with_tag(state['game'], tag)[0]
    shot.set_actor_location(pawn.get_actor_location()-unreal.Vector(250, 0, 0), False, True)
    shot.set_actor_rotation(unreal.Rotator(), False)
    shot.set_actor_enable_collision(True)
    movement = shot.get_component_by_class(unreal.ProjectileMovementComponent)
    movement.set_velocity_in_local_space(unreal.Vector(2500, 0, 0))
    movement.activate(True)


def tick(delta):
    try:
        now = time.monotonic()
        if now - state['started'] > 180:
            raise RuntimeError('Timeout at ' + state['stage'])
        elapsed = now - state['since']
        stage = state['stage']
        if stage == 'begin' and elapsed > 3:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
            state.update(stage='world', since=now)
        elif stage == 'world':
            game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                return
            player = unreal.GameplayStatics.get_player_pawn(game, 0)
            if not player:
                return
            pawn = unreal.GameplayStatics.get_all_actors_of_class(game, cls)[0]
            check(isinstance(pawn.get_controller(), unreal.ChasingAIController), 'AI possession')
            check(pawn.get_current_health() == 150, 'Initial health 150')
            player.set_actor_location(unreal.Vector(750, 0, 100), False, True)
            player.character_movement.disable_movement()
            pawn.mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            pawn.mesh.set_editor_property('enable_update_rate_optimizations', False)
            state.update(stage='chase', since=now, game=game, player=player, pawn=pawn,
                initial=(pawn.get_actor_location()-player.get_actor_location()).length())
        elif stage == 'chase' and elapsed > 1:
            pawn, player = state['pawn'], state['player']
            check((pawn.get_actor_location()-player.get_actor_location()).length() < state['initial']-100, 'Clown approaches player')
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'BS_ClownMonster_Locomotion', 'Velocity-driven locomotion')
            check(pawn.mesh.get_socket_location('head').z-pawn.mesh.get_socket_location('foot_l').z > 100, 'Animation fits the Clown skeleton')
            pawn.character_movement.disable_movement()
            player.set_actor_location(pawn.get_actor_location()+unreal.Vector(110, 0, 20), False, True)
            state.update(stage='contact', since=now, energy=player.get_current_energy())
        elif stage == 'contact' and elapsed > .6:
            pawn, player = state['pawn'], state['player']
            check(player.get_current_energy() < state['energy'], 'Contact drains player energy')
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'anim_attack_1', 'Contact plays Clown attack')
            player.set_actor_location(unreal.Vector(1700, 0, 100), False, True)
            state.update(stage='leave', since=now)
        elif stage == 'leave' and elapsed > .4:
            pawn = state['pawn']
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'BS_ClownMonster_Locomotion', 'Leaving contact restores locomotion')
            state.update(stage='shot', since=now, before=pawn.get_actor_location())
            shoot(unreal.BallProjectile, pawn)
        elif stage == 'shot' and elapsed > .5:
            pawn = state['pawn']
            check(pawn.get_current_health() == 125, 'Actual yellow projectile inflicts 25 damage')
            check(pawn.get_actor_location().x-state['before'].x > 20, 'Projectile pushes Clown backwards')
            unreal.GameplayStatics.apply_damage(pawn, 85, None, None, unreal.DamageType)
            state.update(stage='lethal_shot', since=now)
        elif stage == 'lethal_shot' and elapsed > .4:
            shoot(unreal.CannonballProjectile, state['pawn'])
            state.update(stage='death', since=now)
        elif stage == 'death' and elapsed > .3:
            pawn = state['pawn']
            reaction = pawn.get_component_by_class(unreal.NPCHitReactionComponent)
            check(pawn.is_dead(), 'Actual red projectile starts death')
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'anim_death', 'Clown uses its own complete falling animation')
            check(reaction.get_current_forward_bend_angle() == 0, 'Death clears the hit bend')
            check(not pawn.get_component_by_class(unreal.NPCHealthBarComponent).is_visible(), 'Death hides health bar')
            state.update(stage='land', since=now)
        elif stage == 'land' and elapsed > 2.3:
            pawn = state['pawn']
            check(pawn.mesh.get_socket_location('head').z < 40, 'Clown head lies near the ground')
            check(pawn.mesh.get_socket_location('pelvis').z < 30, 'Clown pelvis lies near the ground')
            state.update(stage='cleanup', since=now)
        elif stage == 'cleanup' and elapsed > 3:
            check(not unreal.GameplayStatics.get_all_actors_of_class(state['game'], cls), 'Corpse removed after death animation')
            point = unreal.GameplayStatics.get_all_actors_of_class(state['game'], spawn_cls)[0]
            check(point.spawn_npc_type('ClownMonster', 1) > 0, 'Spawn point accepts ClownMonster alias')
            state.update(stage='spawn', since=now)
        elif stage == 'spawn' and elapsed > 1:
            spawned = unreal.GameplayStatics.get_all_actors_of_class(state['game'], cls)
            check(len(spawned) == 1, 'Spawn point creates one Clown NPC')
            check(isinstance(spawned[0].get_controller(), unreal.ChasingAIController), 'Spawned Clown has active AI')
            finish()
    except Exception:
        finish(traceback.format_exc())


state['handle'] = unreal.register_slate_post_tick_callback(tick)
