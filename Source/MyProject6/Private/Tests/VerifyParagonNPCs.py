"""Fresh-process persistence, appearance, AI, damage and portal-spawn validation.

Use -ExecutePythonScript with a real RHI and -RenderOffscreen. This creates a
disposable world and never saves any level or existing vendor asset.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = (Path(unreal.Paths.project_saved_dir()) / 'CharacterMigration').resolve()
DATA = json.loads((OUT / 'NPCs.json').read_text())
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
state = {'stage': 'preview', 'since': time.monotonic(), 'started': time.monotonic(),
         'checks': [], 'captures': [], 'index': 0, 'preview_index': 0}


def check(condition, message):
    assert condition, message
    state['checks'].append(message)


def asset_path(obj):
    return obj.get_path_name() if obj else None


def finish(error=None):
    if 'handle' in state:
        unreal.unregister_slate_post_tick_callback(state['handle'])
    (OUT / 'Verification.json').write_text(json.dumps({'passed': error is None, 'error': error,
        'checks': state['checks'], 'captures': state['captures']}, indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.execute_console_command(None, 'QUIT_EDITOR')


def advance(stage):
    state.update(stage=stage, since=time.monotonic())


def tick(delta):
    try:
        now = time.monotonic()
        elapsed = now - state['since']
        if now - state['started'] > 900:
            raise RuntimeError('Timeout at ' + state['stage'])
        stage = state['stage']
        if stage == 'preview' and elapsed > 8:
            pack = state['packs'][state['preview_index']]
            selected = [a for entry,a in state['previews'] if entry['spec']['pack'] == pack]
            for entry, actor in state['previews']:
                actor.set_actor_hidden_in_game(entry['spec']['pack'] != pack)
                actor.set_is_temporarily_hidden_in_editor(entry['spec']['pack'] != pack)
            for index, actor in enumerate(selected):
                actor.set_actor_location(unreal.Vector(0, (index-(len(selected)-1)/2)*310, actor.capsule_component.get_scaled_capsule_half_height()+2), False, True)
            distance = max(900, len(selected)*280)
            state['camera'].set_actor_location(unreal.Vector(distance, -distance*.12, 460), False, True)
            state['camera'].set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(state['camera'].get_actor_location(), unreal.Vector(0,0,130)), False)
            advance('capture')
        elif stage == 'capture' and elapsed > 4:
            path = str(OUT / (state['packs'][state['preview_index']] + '.png'))
            state['captures'].append(path)
            # Screenshot preparation pumps Slate callbacks while compiling assets.
            # Switch state before calling it to prevent recursive screenshot requests.
            advance('capturing')
            state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1800, 900, path, state['camera'], False, False)
            advance('next_preview')
        elif stage == 'next_preview' and elapsed > 3:
            state['preview_index'] += 1
            if state['preview_index'] < len(state['packs']):
                advance('preview')
            else:
                for entry, actor in state['previews']:
                    ACTORS.destroy_actor(actor)
                # One placed example tests normal placement; later characters use
                # the actual project portal API and saved class defaults.
                first = state['base'][0]
                state['first'] = ACTORS.spawn_actor_from_class(first['class'], unreal.Vector(0, 0, first['spec']['half']+3))
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                advance('world')
        elif stage == 'world':
            game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not game:
                return
            player = unreal.GameplayStatics.get_player_pawn(game, 0)
            if not player:
                return
            state.update(game=game, player=player,
                         point=unreal.GameplayStatics.get_all_actors_of_class(game, unreal.NPCSpawnPoint)[0])
            player.character_movement.disable_movement()
            advance('spawn')
        elif stage == 'spawn':
            entry = state['base'][state['index']]
            player = state['player']
            player.set_actor_location(unreal.Vector(1000, 0, 100), False, True)
            if state['index'] > 0:
                point = state['point']
                types = dict(point.get_editor_property('npc_types'))
                types['VerifyParagon'] = entry['class']
                point.set_editor_property('npc_types', types)
                accepted = point.spawn_npc_type('VerifyParagon', 1)
                # The API returns a positive request ID, not the spawned count.
                check(accepted > 0, entry['spec']['name'] + ': portal accepts saved NPC class')
            advance('find_pawn')
        elif stage == 'find_pawn' and elapsed > .3:
            entry = state['base'][state['index']]
            pawns = unreal.GameplayStatics.get_all_actors_of_class(state['game'], entry['class'])
            if not pawns:
                return
            pawn = pawns[0]
            state['pawn'] = pawn
            pawn.mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            check(isinstance(pawn.get_controller(), unreal.ChasingAIController), entry['spec']['name'] + ': AI possesses placed/spawned NPC')
            check(pawn.get_current_health() == 150, entry['spec']['name'] + ': health initialized')
            check(asset_path(pawn.mesh.get_skeletal_mesh_asset()) == entry['mesh'], entry['spec']['name'] + ': spawn inherits mesh')
            state['initial_distance'] = (pawn.get_actor_location()-state['player'].get_actor_location()).length()
            advance('chase')
        elif stage == 'chase' and elapsed > 1.1:
            pawn, player = state['pawn'], state['player']
            entry = state['base'][state['index']]
            check((pawn.get_actor_location()-player.get_actor_location()).length() < state['initial_distance']-100,
                  entry['spec']['name'] + ': follows player')
            check(asset_path(pawn.mesh.get_anim_instance().get_animation_asset()) == entry['blend'], entry['spec']['name'] + ': velocity-driven locomotion')
            pawn.character_movement.disable_movement()
            player.set_actor_location(pawn.get_actor_location()+unreal.Vector(115,0,0), False, True)
            state['energy'] = player.get_current_energy()
            advance('contact')
        elif stage == 'contact' and elapsed > .5:
            pawn, player = state['pawn'], state['player']
            entry = state['base'][state['index']]
            check(player.get_current_energy() < state['energy'], entry['spec']['name'] + ': contact drains energy')
            check(asset_path(pawn.mesh.get_anim_instance().get_animation_asset()) == entry['animations']['attack'], entry['spec']['name'] + ': contact attack animation')
            player.set_actor_location(unreal.Vector(1800,0,100), False, True)
            advance('damage')
        elif stage == 'damage' and elapsed > .3:
            pawn = state['pawn']
            entry = state['base'][state['index']]
            check(asset_path(pawn.mesh.get_anim_instance().get_animation_asset()) == entry['blend'], entry['spec']['name'] + ': resumes locomotion')
            unreal.GameplayStatics.apply_damage(pawn, 25, None, None, unreal.DamageType)
            check(pawn.get_current_health() == 125, entry['spec']['name'] + ': receives damage')
            advance('death')
        elif stage == 'death' and elapsed > .5:
            pawn = state['pawn']
            entry = state['base'][state['index']]
            unreal.GameplayStatics.apply_damage(pawn, 125, None, None, unreal.DamageType)
            check(pawn.is_dead(), entry['spec']['name'] + ': lethal damage starts death')
            check(asset_path(pawn.mesh.get_anim_instance().get_animation_asset()) == entry['animations']['death'], entry['spec']['name'] + ': matching death animation')
            check(not pawn.get_actor_enable_collision(), entry['spec']['name'] + ': corpse collision disabled')
            advance('cleanup')
        elif stage == 'cleanup' and elapsed > 5.5:
            entry = state['base'][state['index']]
            check(not unreal.GameplayStatics.get_all_actors_of_class(state['game'], entry['class']), entry['spec']['name'] + ': corpse cleaned up')
            state['index'] += 1
            if state['index'] < len(state['base']):
                advance('spawn')
            else:
                finish()
    except Exception:
        finish(traceback.format_exc())


try:
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    content = Path(unreal.Paths.project_content_dir())
    for pack in ('ParagonCrunch', 'ParagonFengMao', 'ParagonGRIMexe'):
        check(not (content / pack).exists(), pack + ': original Content root removed')
        check((content / 'Characters' / pack).is_dir(), pack + ': package moved to Characters')
    state['base'] = []
    for entry in DATA['npcs']:
        bp = unreal.load_asset(entry['blueprint'])
        check(bool(bp), entry['blueprint'] + ': reloads from disk')
        cls = bp.generated_class()
        defaults = unreal.get_default_object(cls)
        check(isinstance(defaults, unreal.WhisperFollower), bp.get_name() + ': NPC gameplay inherited')
        check(asset_path(defaults.mesh.get_skeletal_mesh_asset()) == entry['mesh'], bp.get_name() + ': mesh persisted')
        rotation = defaults.mesh.get_editor_property('relative_rotation')
        check(abs(rotation.pitch) < .01 and abs(rotation.roll) < .01 and abs(rotation.yaw+90) < .01,
              bp.get_name() + ': mesh upright and facing forward')
        check(asset_path(defaults.get_editor_property('locomotion_blend_space')) == entry['blend'], bp.get_name() + ': blend persisted')
        check(defaults.get_editor_property('auto_possess_ai') == unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED, bp.get_name() + ': placed and spawned AI enabled')
        check(defaults.get_editor_property('auto_possess_player') == unreal.AutoReceiveInput.DISABLED, bp.get_name() + ': player possession disabled')
        mesh = defaults.mesh.get_skeletal_mesh_asset()
        check(all(slot.material_interface for slot in mesh.materials), bp.get_name() + ': all materials resolve')
        old_path = entry['mesh'].replace('/Game/NPC/', '/Game/', 1)
        check(unreal.load_object(None, old_path) == mesh, bp.get_name() + ': old references resolve to moved mesh')
        for key, prop in [('attack','contact_attack_animation'), ('death','death_animation')]:
            anim = defaults.get_editor_property(prop)
            check(asset_path(anim) == entry['animations'][key] and anim.get_editor_property('skeleton') == mesh.skeleton,
                  bp.get_name() + ': ' + key + ' compatible and persistent')
        entry['class'] = cls
        if entry['base']:
            state['base'].append(entry)
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    floor = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-10))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(80,80,.2))
    ACTORS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(1000,0,100), unreal.Rotator(pitch=0,yaw=180,roll=0))
    point = ACTORS.spawn_actor_from_class(unreal.load_asset('/Game/GameplayActors/NPCSpawner/BP_NPCSpawnPoint').generated_class(), unreal.Vector(0,0,0))
    point.set_editor_property('spawn_on_begin_play', False)
    point.set_actor_hidden_in_game(True)
    point.set_is_temporarily_hidden_in_editor(True)
    settings = point.get_editor_property('spawn_settings')
    settings.set_editor_property('spawn_radius', 0.)
    point.set_editor_property('spawn_settings', settings)
    state['previews'] = []
    for index, entry in enumerate(DATA['npcs']):
        actor = ACTORS.spawn_actor_from_class(entry['class'], unreal.Vector(0,index*320,entry['spec']['half']+2))
        actor.mesh.set_update_animation_in_editor(True)
        actor.mesh.play_animation(unreal.load_asset(entry['animations']['idle']), True)
        state['previews'].append((entry, actor))
    state['packs'] = [e['spec']['pack'] for e in state['base']]
    state['camera'] = ACTORS.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(2000,0,400))
    state['camera'].camera_component.set_editor_property('field_of_view', 65.)
    state['camera'].camera_component.set_editor_property('aspect_ratio', 2.)
    for rotation,intensity in [(unreal.Rotator(pitch=-35,yaw=150,roll=0),10.),(unreal.Rotator(pitch=-25,yaw=-30,roll=0),5.)]:
        light = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,600), rotation)
        light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(intensity)
    for command in ['r.DefaultFeature.AutoExposure 0','r.EyeAdaptationQuality 0','r.TonemapperGamma 2.2','t.MaxFPS 60']:
        unreal.SystemLibrary.execute_console_command(world, command)
    ACTORS.set_selected_level_actors([])
    unreal.EditorLevelLibrary.editor_set_game_view(True)
    state['started'] = state['since'] = time.monotonic()
    if '-ParagonSkipPreview' in unreal.SystemLibrary.get_command_line():
        # Reuse inspected captures when only runtime test code changed.
        state['captures'] = [str(OUT / (pack + '.png')) for pack in state['packs']]
        assert all(Path(path).is_file() for path in state['captures'])
        state.update(stage='next_preview', preview_index=len(state['packs'])-1,
                     since=time.monotonic()-4)
    state['handle'] = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(traceback.format_exc())
