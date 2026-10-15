"""Capture the three NPC health bars in a disposable PIE scene; save no map changes."""
import json
import time
from pathlib import Path
import unreal

output = (Path(unreal.Paths.project_saved_dir()) / 'NPCHealth/Rendering').resolve()
output.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 30, .2))
wall = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-220, 800, 160))
wall.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
wall.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
wall.static_mesh_component.set_collision_profile_name('BlockAll')
wall.set_actor_scale3d(unreal.Vector(.2, 1.0, 3.5))
wall.set_editor_property('tags', ['NPCHealthTestWall'])
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-35, yaw=0, roll=0))
light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
light.light_component.set_editor_property('intensity', 5.0)
actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
post = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
post.set_editor_property('unbound', True)
settings = post.get_editor_property('settings')
settings.set_editor_property('override_auto_exposure_method', True)
settings.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
settings.set_editor_property('override_auto_exposure_bias', True)
settings.set_editor_property('auto_exposure_bias', 10.0)
post.set_editor_property('settings', settings)
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-450, 0, 100))
contact_camera_position = unreal.Vector(-340, -220, 210)
contact_camera = actors.spawn_actor_from_class(unreal.CameraActor, contact_camera_position,
    unreal.MathLibrary.find_look_at_rotation(contact_camera_position, unreal.Vector(0, 0, 100)))
contact_camera.set_editor_property('tags', ['WhisperContactCamera'])
classes = [unreal.ChasingNPC,
           unreal.load_class(None, '/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C'),
           unreal.load_class(None, '/Game/NPC/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C')]
for cls, y in zip(classes, [-190, 0, 190]):
    npc = actors.spawn_actor_from_class(cls, unreal.Vector(0, y, 100), unreal.Rotator(pitch=0, yaw=180, roll=0))
    if isinstance(npc, unreal.ChasingNPC):
        npc.set_editor_property('spawn_replacements_on_death', False)
    if isinstance(npc, unreal.RadicalMikeNPC):
        npc.set_editor_property('enable_shooting', False)
    npc.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)

state = dict(stage='start', started=time.monotonic(), since=time.monotonic(), captures=[])


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (output / 'result.json').write_text(json.dumps(dict(passed=error is None, error=error,
                                                     captures=state['captures']), indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    if error:
        unreal.log_error('NPC_HEALTH_RENDER_FAILED ' + error)
    else:
        unreal.log('NPC_HEALTH_RENDER_OK')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def capture(name):
    path = output / (name + '.png')
    unreal.SystemLibrary.execute_console_command(state['world'], f'Shot SHOWUI filename="{path}" -nosuffix')
    state['captures'].append(str(path))


def tick(delta):
    try:
        now = time.monotonic()
        if now - state['started'] > 150:
            raise RuntimeError('Timeout at ' + state['stage'])
        if state['stage'] == 'start':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if world is None:
                return
            player = unreal.GameplayStatics.get_player_pawn(world, 0)
            if player is None:
                return
            player.set_actor_location(unreal.Vector(-450, 0, 100), False, True)
            player.get_controller().set_control_rotation(unreal.Rotator(pitch=-3, yaw=0, roll=0))
            player.character_movement.disable_movement()
            npcs = []
            for cls in classes:
                candidates = unreal.GameplayStatics.get_all_actors_of_class(world, cls)
                npc = next(a for a in candidates if not isinstance(a, unreal.RadicalMikeNPC)) if cls == unreal.ChasingNPC else candidates[0]
                npc.character_movement.disable_movement()
                npcs.append(npc)
            state.update(world=world, player=player, npcs=npcs, stage='full', since=now)
            return
        wait_seconds = .35 if state['stage'] in ['claw_one', 'claw_two', 'leave_contact',
                                               'ghoul_claw_one', 'ghoul_claw_two', 'leave_ghoul'] else 5
        if now - state['since'] < wait_seconds:
            return
        if state['stage'] == 'full':
            for npc in state['npcs']:
                bar = npc.get_component_by_class(unreal.NPCHealthBarComponent)
                head_name = 'head_01' if isinstance(npc, unreal.RadicalMikeNPC) else 'head'
                head_z = npc.mesh.get_socket_location(head_name).z
                assert abs(bar.get_world_location().z - head_z - bar.get_editor_property('head_clearance')) < 1
                unreal.log('NPC_BAR_POSITION ' + npc.get_class().get_name() + ' actor=' + str(npc.get_actor_location())
                           + ' bar=' + str(bar.get_world_location()) + ' relative=' + str(bar.get_editor_property('relative_location'))
                           + ' head=' + str(npc.mesh.get_socket_location(head_name)))
            capture('full')
            state.update(stage='damage', since=now)
        elif state['stage'] == 'damage':
            for npc in state['npcs']:
                unreal.GameplayStatics.apply_damage(npc, npc.get_max_health() * 0.5, None, None, unreal.DamageType)
                assert npc.get_current_health() == npc.get_max_health() * 0.5
            state.update(stage='half', since=now)
        elif state['stage'] == 'half':
            capture('half')
            state.update(stage='color', since=now)
        elif state['stage'] == 'color':
            state['npcs'][1].get_component_by_class(unreal.NPCHealthBarComponent).set_bar_color(unreal.LinearColor(0.6, 0.05, 1.0, 1.0))
            state.update(stage='colored', since=now)
        elif state['stage'] == 'colored':
            capture('custom_color')
            state.update(stage='place_cover', since=now)
        elif state['stage'] == 'place_cover':
            wall = unreal.GameplayStatics.get_all_actors_with_tag(state['world'], 'NPCHealthTestWall')[0]
            wall.set_actor_location(unreal.Vector(-220, 0, 160), False, True)
            state.update(wall=wall, stage='covered', since=now)
        elif state['stage'] == 'covered':
            bar = state['npcs'][1].get_component_by_class(unreal.NPCHealthBarComponent)
            assert not bar.is_visible(), 'Whisper bar must be hidden behind the wall'
            capture('behind_wall')
            state.update(stage='uncover', since=now)
        elif state['stage'] == 'uncover':
            state['wall'].destroy_actor()
            state.update(stage='restored', since=now)
        elif state['stage'] == 'restored':
            assert state['npcs'][1].get_component_by_class(unreal.NPCHealthBarComponent).is_visible(), 'Bar did not return after removing cover'
            capture('visible_again')
            state.update(stage='contact', since=now)
        elif state['stage'] == 'contact':
            player = state['player']
            state['whisper_energy_before'] = player.get_current_energy()
            player.set_actor_location(unreal.Vector(-125, 0, 100), False, True)
            player.set_actor_hidden_in_game(True)
            camera = unreal.GameplayStatics.get_all_actors_with_tag(state['world'], 'WhisperContactCamera')[0]
            player.get_controller().set_view_target_with_blend(camera, 0)
            state.update(stage='claw_one', since=now)
        elif state['stage'] == 'claw_one':
            whisper = state['npcs'][1]
            assert whisper.mesh.get_material(0) == whisper.get_editor_property('contact_glow_material'), 'Whisper did not glow on contact'
            assert whisper.mesh.get_anim_instance().get_animation_asset() == whisper.get_editor_property('contact_attack_animation')
            assert state['player'].get_current_energy() < state['whisper_energy_before'], 'Whisper must drain player energy during contact'
            capture('whisper_claw_one')
            state.update(stage='claw_two', since=now)
        elif state['stage'] == 'claw_two':
            capture('whisper_claw_two')
            state.update(stage='leave_contact', since=now)
        elif state['stage'] == 'leave_contact':
            state['player'].set_actor_location(unreal.Vector(-450, 0, 100), False, True)
            state.update(stage='contact_ended', since=now)
        elif state['stage'] == 'contact_ended':
            whisper = state['npcs'][1]
            assert whisper.mesh.get_material(0) != whisper.get_editor_property('contact_glow_material'), 'Whisper glow did not end'
            assert whisper.mesh.get_anim_instance().get_animation_asset() == whisper.get_editor_property('locomotion_blend_space')
            capture('whisper_contact_ended')
            state.update(stage='ghoul_contact', since=now)
        elif state['stage'] == 'ghoul_contact':
            state['ghoul_energy_before'] = state['player'].get_current_energy()
            state['player'].set_actor_location(unreal.Vector(-125, -190, 100), False, True)
            camera = unreal.GameplayStatics.get_all_actors_with_tag(state['world'], 'WhisperContactCamera')[0]
            position = unreal.Vector(-340, -410, 210)
            camera.set_actor_location(position, False, True)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, -190, 100)), True)
            state.update(stage='ghoul_claw_one', since=now)
        elif state['stage'] == 'ghoul_claw_one':
            ghoul = state['npcs'][0]
            assert ghoul.mesh.get_anim_instance().get_animation_asset() == ghoul.get_editor_property('contact_attack_animation')
            assert state['player'].get_current_energy() < state['ghoul_energy_before'], 'Ghoul must still drain player energy'
            capture('ghoul_claw_one')
            state.update(stage='ghoul_claw_two', since=now)
        elif state['stage'] == 'ghoul_claw_two':
            capture('ghoul_claw_two')
            state.update(stage='leave_ghoul', since=now)
        elif state['stage'] == 'leave_ghoul':
            state['player'].set_actor_location(unreal.Vector(-450, 0, 100), False, True)
            state['energy_after_contacts'] = state['player'].get_current_energy()
            state.update(stage='ghoul_contact_ended', since=now)
        elif state['stage'] == 'ghoul_contact_ended':
            ghoul = state['npcs'][0]
            assert ghoul.mesh.get_animation_mode() == unreal.AnimationMode.ANIMATION_BLUEPRINT, 'Ghoul locomotion did not return'
            assert state['player'].get_current_energy() == state['energy_after_contacts'], 'Energy must stop draining away from the NPCs'
            capture('ghoul_contact_ended')
            state.update(stage='finish', since=now)
        elif state['stage'] == 'finish':
            assert all(Path(path).is_file() for path in state['captures']), 'Screenshot was not written'
            finish()
    except Exception as error:
        finish(str(error))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
