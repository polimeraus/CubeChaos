"""Verify BeginPlay/runtime rebuilding and render the saved showcase in disposable PIE."""
import json
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'VoxelShape/Preview'
out.mkdir(parents=True, exist_ok=True)
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/Parametric/VoxelShape/Maps/Lvl_VoxelShapePreview')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector())
state = {'stage': 'world', 'start': time.monotonic(), 'index': 0, 'frames': 0}
views = ['Overview', 'Heart', 'HeartDepth4', 'SolidCubes']
results = {}

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    results.update(passed=error is None, error=error)
    (out / 'RuntimeVerification.json').write_text(json.dumps(results, indent=2))
    if error:
        unreal.log_error('VOXEL_RENDER_FAILED ' + error)
    else:
        unreal.log('VOXEL_RUNTIME_AND_RENDER_OK ' + json.dumps(results))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def prepare_view():
    index = state['index']
    heart, camera = state['heart'], state['camera']
    if index == 0:
        position, look = unreal.Vector(1450, -5300, 1950), unreal.Vector(0, 0, 680)
    else:
        for actor in state['shapes']:
            actor.set_actor_hidden_in_game(actor != heart)
        center = heart.get_actor_location()
        position, look = center + unreal.Vector(650, -1700, 420), center
        if index == 2:
            count = heart.generated_cube_count
            heart.configure_shape(unreal.VoxelShapePreset.HEART, 15, 4)
            assert heart.generated_cube_count == count * 2 and heart.generated_grid.y == 4
            results['runtime_depth_rebuild'] = True
        elif index == 3:
            heart.set_editor_property('appearance', unreal.VoxelShapeAppearance.SOLID_CUBES)
            heart.set_editor_property('cube_gap', 3.)
            heart.set_editor_property('emissive_intensity', .7)
            heart.configure_shape(unreal.VoxelShapePreset.HEART, 15, 2)
            assert heart.solid_cubes.get_instance_count() == heart.generated_cube_count
            assert heart.wire_edges.get_instance_count() == 0
            results['runtime_appearance_rebuild'] = True
    camera.set_actor_location(position, False, False)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, look), False)
    state.update(stage='capture', frames=5, next=time.monotonic()+3)

def tick(_dt):
    try:
        now = time.monotonic()
        if now - state['start'] > 180:
            raise RuntimeError('Render timed out: ' + state['stage'])
        if state['stage'] == 'world':
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not world:
                return
            # Preview-process settings only; do not save or change the project's quality settings.
            for command in ('r.BloomQuality 5', 'r.AntiAliasingMethod 1',
                            'ShowFlag.PostProcessing 1', 'ShowFlag.Bloom 1', 'ShowFlag.Tonemapper 1'):
                unreal.SystemLibrary.execute_console_command(world, command)
            shapes = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ParametricVoxelShape)
            cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D)
            if len(shapes) != 6 or not cameras:
                return
            assert all(a.generated_cube_count > 0 and not a.generation_warning and not a.is_actor_tick_enabled() for a in shapes)
            heart = next(a for a in shapes if a.shape == unreal.VoxelShapePreset.HEART)
            camera = cameras[0]
            capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.set_editor_property('show_flag_settings', [
                unreal.EngineShowFlagsSetting(show_flag_name=name, enabled=True)
                for name in ('PostProcessing', 'Bloom', 'Tonemapper', 'AntiAliasing')
            ])
            for key, value in {'capture_every_frame': False, 'capture_on_movement': False,
                               'always_persist_rendering_state': True, 'fov_angle': 38.,
                               'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
                capture.set_editor_property(key, value)
            post = capture.get_editor_property('post_process_settings')
            for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                               'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                               'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.,
                               'override_bloom_intensity': True, 'bloom_intensity': .65}.items():
                post.set_editor_property(key, value)
            capture.set_editor_property('post_process_settings', post)
            target = unreal.RenderingLibrary.create_render_target2d(world, 1440, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            capture.set_editor_property('texture_target', target)
            state.update(world=world, shapes=shapes, heart=heart, camera=camera, capture=capture, target=target)
            results.update(pie_begin_play=True, six_presets_generated=True, no_idle_actor_tick=True)
            prepare_view()
            state['next'] = time.monotonic()+12
        elif now >= state['next']:
            if state['stage'] == 'capture':
                state['capture'].capture_scene()
                state['frames'] -= 1
                state['next'] = time.monotonic()+.6
                if state['frames'] == 0:
                    state['stage'] = 'export'
            elif state['stage'] == 'export':
                unreal.RenderingLibrary.export_render_target(state['world'], state['target'], str(out), views[state['index']]+'.png')
                state['index'] += 1
                if state['index'] == len(views):
                    finish()
                else:
                    prepare_view()
    except Exception as error:
        finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
