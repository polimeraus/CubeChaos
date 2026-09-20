"""Render all shape/icon combinations in a disposable PIE world.

UnrealEditor-Cmd <project> /Engine/Maps/Entry -ExecutePythonScript=<this file>
  -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -RenderOffscreen -unattended
"""
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / 'Pickups/Preview'
output.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset('/Game/Gameplay/Pickups/BP_ParametricPickup')
assert bp, 'Run SetupParametricPickup.py first'
shapes = [unreal.PickupShape.CUBE, unreal.PickupShape.SPHERE, unreal.PickupShape.ROUNDED_CUBE, unreal.PickupShape.PYRAMID]
icons = [unreal.PickupMotif.STAR, unreal.PickupMotif.QUESTION_MARK, unreal.PickupMotif.CRESCENT,
         unreal.PickupMotif.CROSS, unreal.PickupMotif.ROCKET, unreal.PickupMotif.BOMB,
         unreal.PickupMotif.AERIAL_BOMB, unreal.PickupMotif.RETRO_ROCKET,
         unreal.PickupMotif.RADIATION_BOMB, unreal.PickupMotif.BOMB_CLUSTER]
icon_names = ['Star', 'Question Mark', 'Crescent', 'Cross', 'Rocket', 'Bomb',
              'Aerial Bomb', 'Retro Rocket', 'Radiation Bomb', 'Bomb Cluster']
colors = [(1, .5, .025), (.035, .7, 1), (.4, .06, 1), (.08, 1, .3), (1, .06, .12), (1, .27, .025),
          (.03, .7, 1), (1, .13, .06), (1, .8, .025), (.16, 1, .48)]
combination_count = len(shapes) * len(icons)
centre_x = (len(icons) - 1) * 85
for row, shape in enumerate(shapes):
    for col, icon in enumerate(icons):
        pickup = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector((len(icons) - 1 - col) * 170, row * 190, 0), unreal.Rotator(yaw=-12))
        for key, value in {'shape': shape, 'icon': icon, 'size': 100., 'corner_radius': 17.,
                           'icon_color': unreal.LinearColor(*colors[col], 1), 'auto_collect': False,
                           'pulse_period': 2., 'maximum_glow': 4.}.items():
            pickup.set_editor_property(key, value)
        pickup.refresh_pickup()
        pickup.set_actor_label(str(shape) + '_' + str(icon))

floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(centre_x, 270, -8))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 30, .12))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(pitch=-55, yaw=-40))
light.light_component.set_editor_property('intensity', 3)
fill = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(pitch=-25, yaw=130))
fill.light_component.set_editor_property('intensity', 1.2)
position = unreal.Vector(centre_x + 270, -1150, 1510)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(centre_x, 270, 10)), False)
state = {'stage': 'world', 'started': time.monotonic()}
result = {}

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    result.update(passed=error is None, error=error)
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    (unreal.log_error if error else unreal.log)('PICKUP_PREVIEW ' + str(result))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def capture(name):
    state['capture'].capture_scene()
    unreal.RenderingLibrary.export_render_target(state['world'], state['target'], str(output), name + '.png')

def phase(value):
    game_time = unreal.GameplayStatics.get_time_seconds(state['world'])
    for pickup in state['pickups']:
        pickup.set_editor_property('pulse_phase', (value - game_time / 2) % 1)
        pickup.refresh_material()

def focus_icon(index):
    pickup = next(p for p in state['pickups'] if p.get_editor_property('icon') == icons[index]
                  and p.get_editor_property('shape') == unreal.PickupShape.ROUNDED_CUBE)
    pickup.set_editor_property('pulse', False)
    pickup.refresh_material()
    focus = pickup.get_actor_location() + unreal.Vector(0, 0, 50)
    camera = state['capture'].get_owner()
    camera.set_actor_location(focus + unreal.Vector(160, -350, 190), False, True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), focus), False)
    state['capture'].set_editor_property('ortho_width', 195)
    state.update(detail_index=index, stage='detail', since=time.monotonic())

def tick(_):
    try:
        now = time.monotonic()
        if now - state['started'] > 180:
            raise RuntimeError('Preview timeout at ' + state['stage'])
        if state['stage'] == 'world':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not world:
                return
            pickups = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ParametricPickup)
            cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if len(pickups) != combination_count or not cameras:
                return
            component = cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            component.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            component.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
            component.set_editor_property('ortho_width', 1900)
            component.set_editor_property('capture_every_frame', False)
            component.set_editor_property('capture_on_movement', False)
            post = component.get_editor_property('post_process_settings')
            post.set_editor_property('override_auto_exposure_method', True)
            post.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
            post.set_editor_property('override_auto_exposure_apply_physical_camera_exposure', True)
            post.set_editor_property('auto_exposure_apply_physical_camera_exposure', False)
            post.set_editor_property('override_auto_exposure_bias', True)
            post.set_editor_property('auto_exposure_bias', 0)
            component.set_editor_property('post_process_settings', post)
            target = unreal.RenderingLibrary.create_render_target2d(world, 2400, 1400, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property('texture_target', target)
            state.update(world=world, pickups=pickups, capture=component, target=target, stage='warmup', since=now)
        elif state['stage'] == 'warmup' and now - state['since'] > 10:
            materials = [p.get_dynamic_material() for p in state['pickups']]
            assert all(isinstance(m, unreal.MaterialInstanceDynamic) for m in materials)
            assert len({m.get_path_name() for m in materials}) == combination_count, 'Shared dynamic materials'
            for index, icon in enumerate(icons):
                matching = [p for p in state['pickups'] if p.get_editor_property('icon') == icon]
                assert len(matching) == len(shapes), icon_names[index]
                assert all(p.get_dynamic_material().get_scalar_parameter_value('Motif') == index for p in matching), icon_names[index]
            phase(0)
            state.update(stage='bright', since=now)
        elif state['stage'] == 'bright' and now - state['since'] > .05:
            capture('all_shapes_bright')
            phase(.5)
            state.update(stage='dim', since=now)
        elif state['stage'] == 'dim' and now - state['since'] > .05:
            capture('all_shapes_dim')
            result['rendered_combinations'] = combination_count
            result['rows_near_to_far'] = ['Cube', 'Sphere', 'Rounded Cube', 'Pyramid']
            result['columns_left_to_right'] = icon_names
            state['target'] = unreal.RenderingLibrary.create_render_target2d(state['world'], 1000, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            state['capture'].set_editor_property('texture_target', state['target'])
            focus_icon(5)
        elif state['stage'] == 'detail' and now - state['since'] > .15:
            index = state['detail_index']
            capture(icon_names[index].lower().replace(' ', '_') + '_detail')
            if index + 1 < len(icons):
                focus_icon(index + 1)
            else:
                # A clean row of the four additions for a readable overview.
                for pickup in state['pickups']:
                    pickup.set_actor_hidden_in_game(True)
                for index in range(6, len(icons)):
                    pickup = next(p for p in state['pickups'] if p.get_editor_property('icon') == icons[index]
                                  and p.get_editor_property('shape') == unreal.PickupShape.ROUNDED_CUBE)
                    pickup.set_actor_hidden_in_game(False)
                    pickup.set_actor_location(unreal.Vector((9 - index) * 150, 0, 0), False, True)
                camera = state['capture'].get_owner()
                focus = unreal.Vector(225, 0, 50)
                camera.set_actor_location(focus + unreal.Vector(100, -850, 430), False, True)
                camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), focus), False)
                state['capture'].set_editor_property('ortho_width', 670)
                state['target'] = unreal.RenderingLibrary.create_render_target2d(state['world'], 1800, 650, unreal.TextureRenderTargetFormat.RTF_RGBA8)
                state['capture'].set_editor_property('texture_target', state['target'])
                state.update(stage='new_icons', since=now)
        elif state['stage'] == 'new_icons' and now - state['since'] > .15:
            capture('new_icons')
            finish()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
