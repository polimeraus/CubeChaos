"""Render all 20 shape/motif combinations in a disposable PIE world.

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
motifs = [unreal.PickupMotif.STAR, unreal.PickupMotif.QUESTION_MARK, unreal.PickupMotif.CRESCENT, unreal.PickupMotif.CROSS, unreal.PickupMotif.ROCKET]
colors = [(1, .5, .025), (.035, .7, 1), (.4, .06, 1), (.08, 1, .3), (1, .06, .12)]
for row, shape in enumerate(shapes):
    for col, motif in enumerate(motifs):
        pickup = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector((4 - col) * 170, row * 190, 0), unreal.Rotator(yaw=-12))
        for key, value in {'shape': shape, 'motif': motif, 'size': 100., 'corner_radius': 17.,
                           'icon_color': unreal.LinearColor(*colors[col], 1), 'auto_collect': False,
                           'pulse_period': 2., 'maximum_glow': 4.}.items():
            pickup.set_editor_property(key, value)
        pickup.refresh_pickup()
        pickup.set_actor_label(str(shape) + '_' + str(motif))

floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(340, 270, -8))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 30, .12))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(pitch=-55, yaw=-40))
light.light_component.set_editor_property('intensity', 3)
fill = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(pitch=-25, yaw=130))
fill.light_component.set_editor_property('intensity', 1.2)
position = unreal.Vector(610, -1150, 1510)
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(340, 270, 10)), False)
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
            if len(pickups) != 20 or not cameras:
                return
            component = cameras[0].get_component_by_class(unreal.SceneCaptureComponent2D)
            component.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            component.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
            component.set_editor_property('ortho_width', 1150)
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
            target = unreal.RenderingLibrary.create_render_target2d(world, 1500, 1100, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property('texture_target', target)
            state.update(world=world, pickups=pickups, capture=component, target=target, stage='warmup', since=now)
        elif state['stage'] == 'warmup' and now - state['since'] > 10:
            materials = [p.get_dynamic_material() for p in state['pickups']]
            assert all(isinstance(m, unreal.MaterialInstanceDynamic) for m in materials)
            assert len({m.get_path_name() for m in materials}) == 20, 'Shared dynamic materials'
            phase(0)
            state.update(stage='bright', since=now)
        elif state['stage'] == 'bright' and now - state['since'] > .05:
            capture('all_shapes_bright')
            phase(.5)
            state.update(stage='dim', since=now)
        elif state['stage'] == 'dim' and now - state['since'] > .05:
            capture('all_shapes_dim')
            result['rendered_combinations'] = 20
            result['rows_near_to_far'] = ['Cube', 'Sphere', 'Rounded Cube', 'Pyramid']
            result['columns_left_to_right'] = ['Star', 'Question Mark', 'Crescent', 'Cross', 'Rocket']
            finish()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
