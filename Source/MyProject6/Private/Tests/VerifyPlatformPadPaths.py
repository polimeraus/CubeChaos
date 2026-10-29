"""Use -SetupPlatformPadPaths once; then run in a fresh process to verify saved assets and render."""
import json
import time
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform'
MAP = ROOT + '/Maps/L_ParametricPlatformCirclesAndPaths'
out = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform' / 'CirclesAndPaths'
out.mkdir(parents=True, exist_ok=True)
setup = '-setupplatformpadpaths' in unreal.SystemLibrary.get_command_line().lower()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(ROOT + '/BP_ParametricPlatform')
assert bp
if setup:
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
defaults = unreal.get_default_object(bp.generated_class())
settings = defaults.get_editor_property('pad_path_settings')
assert settings.get_editor_property('surface_material')
assert len(settings.get_editor_property('pads')) == 5

if setup and not unreal.EditorAssetLibrary.does_asset_exist(MAP):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
    for row, mode in enumerate([unreal.PlatformMode.SOLID, unreal.PlatformMode.CELLULAR]):
        for col, shape in enumerate(['GRID', 'CIRCLE', 'STAR', 'ROUNDED_BLOB']):
            p = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector((col-1.5)*2500, (row-.5)*3000, 0))
            p.set_actor_label(('Solid_' if row == 0 else 'Cellular_') + shape)
            p.set_editor_property('platform_mode', mode)
            p.set_editor_property('outer_shape', getattr(unreal.ChunkedDecorativeFloorShape, shape))
            p.set_editor_property('shape_size', unreal.Vector2D(2100, 1850))
            p.set_editor_property('thickness', 100)
            p.set_editor_property('tile_size', unreal.Vector2D(150, 150))
            p.set_editor_property('border_style', unreal.PlatformBorder.INSET_BAND)
            p.set_editor_property('boundary_inset', 65)
            p.set_editor_property('border_width', 30)
            p.set_editor_property('use_destruction_ripple', False)
            p.set_editor_property('floor_pattern', unreal.ChunkedFloorPattern.CIRCLES_AND_PATHS)
            if col == 3:
                custom = p.get_editor_property('pad_path_settings')
                custom.set_editor_property('pattern_color', unreal.LinearColor(.1, .85, 1, 1))
                paths = list(custom.get_editor_property('paths'))
                paths[0].set_editor_property('waypoints', [unreal.Vector2D(-.35, -.1)])
                custom.set_editor_property('paths', paths)
                p.set_editor_property('pad_path_settings', custom)
            p.rebuild_platform()
    for rot, intensity in [(unreal.Rotator(pitch=-65, yaw=-30), 4), (unreal.Rotator(pitch=-35, yaw=150), 2)]:
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,5000), rot)
        light.light_component.set_editor_property('intensity', intensity)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    unreal.log('PLATFORM_PAD_PATH_SAVED')
else:
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    assert world

platforms = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricPlatform)]
assert len(platforms) == 8
records = []
for p in platforms:
    assert p.get_editor_property('floor_pattern') == unreal.ChunkedFloorPattern.CIRCLES_AND_PATHS
    assert not p.get_editor_property('pad_path_warning'), p.get_editor_property('pad_path_warning')
    s = p.get_editor_property('pad_path_settings')
    assert len(s.get_editor_property('pads')) == 5 and len(s.get_editor_property('paths')) == 4
    components = p.get_components_by_class(unreal.DynamicMeshComponent)
    triangles = sum(c.get_dynamic_mesh().get_triangle_count() for c in components)
    assert triangles > 0
    mid = components[0].get_material(len(p.get_editor_property('floor_materials')) + 1)
    assert mid and mid.get_scalar_parameter_value('PadCount') == 3
    expected_segments = 5 if p.get_actor_label().endswith('ROUNDED_BLOB') else 4
    assert mid.get_scalar_parameter_value('SegmentCount') == expected_segments
    if expected_segments == 5:
        assert s.get_editor_property('pattern_color') == unreal.LinearColor(.1,.85,1,1)
    records.append({'label':p.get_actor_label(), 'triangles':triangles, 'segments':expected_segments})
fresh = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0,15000,0))
fresh.set_floor_pattern(unreal.ChunkedFloorPattern.CIRCLES_AND_PATHS)
assert fresh.get_editor_property('pad_path_settings').get_editor_property('surface_material')
assert not fresh.get_editor_property('pad_path_warning')
actors.destroy_actor(fresh)
(out / ('Setup.json' if setup else 'Persistence.json')).write_text(json.dumps(records, indent=2))
unreal.log('PLATFORM_PAD_PATH_PERSISTENCE_OK ' + json.dumps(records))

if not setup and '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
    # Demonstrate immediate removal of paint with a damaged cell, without modifying the saved map.
    damaged = next(p for p in platforms if p.get_actor_label() == 'Cellular_GRID')
    # Editor preview worlds do not advance gameplay cleanup timers for transient hit flashes.
    damaged.set_editor_property('flash_duration', 0.)
    removed = damaged.destroy_cells_in_radius(damaged.get_actor_location() + unreal.Vector(0,0,-30), 230, unreal.ChunkedFloorBlastShape.SPHERE)
    assert removed > 0
    position = unreal.Vector(0, -5800, 16000)
    camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position,
        unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0,0,0)))
    capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for key, value in {'capture_every_frame':False, 'capture_on_movement':False, 'always_persist_rendering_state':True,
                       'fov_angle':45, 'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
        capture.set_editor_property(key, value)
    post = capture.get_editor_property('post_process_settings')
    for key, value in {'override_auto_exposure_method':True, 'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                       'override_auto_exposure_apply_physical_camera_exposure':True, 'auto_exposure_apply_physical_camera_exposure':False,
                       'override_auto_exposure_bias':True, 'auto_exposure_bias':0}.items():
        post.set_editor_property(key, value)
    capture.set_editor_property('post_process_settings',post)
    target = unreal.RenderingLibrary.create_render_target2d(world, 2000, 1200, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    capture.set_editor_property('texture_target',target)
    state = {'next':time.monotonic()+12, 'captures':0}
    def stop():
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    def tick(_):
        if time.monotonic() < state['next']: return
        try:
            if state['captures'] < 4:
                capture.capture_scene()
                state.update(captures=state['captures']+1, next=time.monotonic()+1)
                return
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'CirclesAndPaths.png')
            unreal.log('PLATFORM_PAD_PATH_RENDERED')
            stop()
        except Exception:
            stop()
            raise
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle'] = unreal.register_slate_post_tick_callback(tick)
