"""Render saved Demon Imp assets and exercise NPC behavior in a disposable PIE world.

Run in a dedicated editor with -ExecutePythonScript=<this file> -RenderOffscreen.
The user's editor and gameplay map are not touched.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = (Path(unreal.Paths.project_saved_dir()) / 'DemonImp').resolve()
ROOT = '/Game/Characters/DemonImp'
state = {'stage': 'preview', 'since': time.monotonic(), 'started': time.monotonic(), 'captures': [], 'checks': []}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40, 40, .2))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(750, 0, 100), unreal.Rotator(0, 180, 0))
cls = unreal.load_class(None, ROOT + '/BP_DemonImp_NPC.BP_DemonImp_NPC_C')
assert cls
npc = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, 76), unreal.Rotator())
npc.mesh.set_update_animation_in_editor(True)
npc.mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
npc.mesh.set_editor_property('enable_update_rate_optimizations', False)
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(450, -240, 190))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 80)), False)
camera.camera_component.set_editor_property('field_of_view', 30.0)
camera.camera_component.set_editor_property('aspect_ratio', 1.0)
for rotation, intensity in [(unreal.Rotator(-35, 150, 0), 10), (unreal.Rotator(-20, -30, 0), 7)]:
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 300), rotation)
    light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(intensity)
fill = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(250, -160, 120))
fill_component = fill.get_component_by_class(unreal.PointLightComponent)
fill_component.set_intensity(100)
fill_component.set_editor_property('attenuation_radius', 2000)
fill_component.set_editor_property('cast_shadows', False)
for cmd in ['r.DefaultFeature.AutoExposure 0', 'r.EyeAdaptationQuality 0', 'r.TonemapperGamma 2.2', 't.MaxFPS 60']:
    unreal.SystemLibrary.execute_console_command(world, cmd)
actors.set_selected_level_actors([])
unreal.EditorLevelLibrary.editor_set_game_view(True)


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (OUT / 'Verification.json').write_text(json.dumps({'passed': error is None, 'error': error,
        'checks': state['checks'], 'captures': state['captures']}, indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.execute_console_command(world, 'QUIT_EDITOR')


def check(condition, message):
    assert condition, message
    state['checks'].append(message)


def tick(delta):
    try:
        now = time.monotonic()
        if now - state['started'] > 180:
            raise RuntimeError('Timeout at ' + state['stage'])
        elapsed = now - state['since']
        stage = state['stage']
        if stage == 'preview' and elapsed > 3:
            npc.mesh.play_animation(unreal.load_asset(ROOT + '/Animations/A_DemonImp_Idle'), True)
            state.update(stage='capture', since=now)
        elif stage == 'capture' and elapsed > 3:
            state.update(stage='begin_play', since=now)
            state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(900, 900, str(OUT / 'Preview.png'), camera, False, False)
            state['captures'].append(str(OUT / 'Preview.png'))
        elif stage == 'begin_play' and elapsed > 2:
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
            check(pawn.get_current_health() == 100, 'Initial health 100')
            player.set_actor_location(unreal.Vector(750, 0, 100), False, True)
            player.character_movement.disable_movement()
            pawn.mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            state.update(stage='chase', since=now, game=game, player=player, pawn=pawn,
                         initial=(pawn.get_actor_location()-player.get_actor_location()).length())
        elif stage == 'chase' and elapsed > 1:
            pawn, player = state['pawn'], state['player']
            check((pawn.get_actor_location()-player.get_actor_location()).length() < state['initial']-100, 'NPC approaches player')
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'BS_DemonImp_Locomotion', 'Velocity-driven locomotion')
            head = pawn.mesh.get_socket_location('Character1_Head_048')
            foot = pawn.mesh.get_socket_location('Character1_LeftFoot_04')
            check(40 < head.z-foot.z < 180, 'Animation matches mesh scale and skeleton')
            facing = pawn.mesh.get_socket_location('jaw_049') - head
            forward = pawn.get_actor_forward_vector()
            check(facing.x*forward.x + facing.y*forward.y > 1, 'Mesh faces the direction of travel')
            state['checks'].append('Head above foot: %.2f cm' % (head.z-foot.z))
            pawn.character_movement.disable_movement()
            player.set_actor_location(pawn.get_actor_location()+unreal.Vector(110, 0, 20), False, True)
            state.update(stage='contact', since=now, energy=player.get_current_energy())
        elif stage == 'contact' and elapsed > .5:
            pawn, player = state['pawn'], state['player']
            check(player.get_current_energy() < state['energy'], 'Contact drains energy')
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'A_DemonImp_Attack', 'Contact plays Demon Imp attack')
            player.set_actor_location(unreal.Vector(1500, 0, 100), False, True)
            state.update(stage='leave_contact', since=now)
        elif stage == 'leave_contact' and elapsed > .3:
            pawn = state['pawn']
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'BS_DemonImp_Locomotion', 'Leaving contact restores locomotion')
            unreal.GameplayStatics.apply_damage(pawn, 25, None, None, unreal.DamageType)
            check(pawn.get_current_health() == 75, 'Damage reduces health to 75')
            state.update(stage='death', since=now)
        elif stage == 'death' and elapsed > .6:
            pawn = state['pawn']
            unreal.GameplayStatics.apply_damage(pawn, 75, None, None, unreal.DamageType)
            check(pawn.is_dead(), 'Lethal damage starts death')
            check(pawn.mesh.get_anim_instance().get_animation_asset().get_name() == 'A_DemonImp_Death', 'Demon Imp death animation')
            check(not pawn.get_actor_enable_collision(), 'Corpse stops blocking')
            state.update(stage='cleanup', since=now)
        elif stage == 'cleanup' and elapsed > 5.5:
            check(not unreal.GameplayStatics.get_all_actors_of_class(state['game'], cls), 'Corpse removed after animation')
            finish()
    except Exception:
        finish(traceback.format_exc())


state['handle'] = unreal.register_slate_post_tick_callback(tick)
