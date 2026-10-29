"""Fresh process: reload saved Blueprint/map, verify both modes and optionally render the examples."""
import json
import time
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform'
out = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform'
out.mkdir(parents=True, exist_ok=True)
blueprint = unreal.load_asset(ROOT + '/BP_ParametricPlatform')
assert blueprint, 'Saved unified Blueprint is missing'
native = unreal.get_default_object(unreal.ParametricPlatform)
defaults = unreal.get_default_object(blueprint.generated_class())
for key in ('platform_mode', 'outer_shape', 'shape_size', 'thickness', 'tile_size', 'border_style',
            'boundary_inset', 'border_width', 'floor_pattern', 'floor_materials', 'palette_tints', 'blob_profile'):
    assert defaults.get_editor_property(key) == native.get_editor_property(key), key
world = unreal.EditorLoadingAndSavingUtils.load_map(ROOT + '/Maps/L_ParametricPlatformExamples')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
platforms = {a.get_actor_label(): a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricPlatform)}
expected = json.loads((out / 'ExpectedExamples.json').read_text())
assert len(platforms) == len(expected) == 14
results = []
for record in expected:
    actor = platforms[record['label']]
    assert actor.get_editor_property('outer_shape') == getattr(unreal.ChunkedDecorativeFloorShape, record['shape'])
    assert actor.get_editor_property('platform_mode') == getattr(unreal.PlatformMode, record['mode'])
    assert actor.get_editor_property('floor_pattern') == getattr(unreal.ChunkedFloorPattern, record['pattern'])
    assert actor.get_editor_property('thickness') == 100
    assert actor.get_actor_location() == unreal.Vector(*record['location'])
    components = actor.get_components_by_class(unreal.DynamicMeshComponent)
    visible = [c for c in components if c.get_dynamic_mesh().get_triangle_count() > 0]
    assert visible, record['label'] + ': transient geometry did not restore on load'
    if record['mode'] == 'SOLID':
        assert actor.get_platform_mesh_count() == 1
        assert actor.get_destructible_cell_count() == 0
    else:
        assert actor.get_destructible_cell_count() > 1
    results.append({'label': record['label'], 'meshes': len(visible),
                    'triangles': sum(c.get_dynamic_mesh().get_triangle_count() for c in visible)})

# A newly placed instance must inherit the saved class defaults too.
fresh = actors.spawn_actor_from_class(blueprint.generated_class(), unreal.Vector(0, 10000, 0))
assert fresh.get_editor_property('platform_mode') == defaults.get_editor_property('platform_mode')
assert fresh.get_platform_mesh_count() == 1
actors.destroy_actor(fresh)
report = {'fresh_process_verified': True, 'saved_blueprint': ROOT + '/BP_ParametricPlatform', 'examples': results}
(out / 'Persistence.json').write_text(json.dumps(report, indent=2))
unreal.log('PARAMETRIC_PLATFORM_PERSISTENCE_OK ' + json.dumps(report))

if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
    position = unreal.Vector(0, -5200, 12000)
    camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, position,
        unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 0)))
    capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for key, value in {'capture_every_frame': False, 'capture_on_movement': False,
                       'always_persist_rendering_state': True, 'fov_angle': 49,
                       'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
        capture.set_editor_property(key, value)
    post = capture.get_editor_property('post_process_settings')
    for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                       'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                       'override_auto_exposure_bias': True, 'auto_exposure_bias': 0}.items():
        post.set_editor_property(key, value)
    capture.set_editor_property('post_process_settings', post)
    target = unreal.RenderingLibrary.create_render_target2d(world, 2100, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    capture.set_editor_property('texture_target', target)
    state = {'next': time.monotonic() + 12, 'captures': 0}
    def stop():
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    def tick(_):
        if time.monotonic() < state['next']:
            return
        try:
            if state['captures'] < 4:
                capture.capture_scene()
                state.update(captures=state['captures'] + 1, next=time.monotonic() + 1)
                return
            unreal.RenderingLibrary.export_render_target(world, target, str(out), 'Platforms.png')
            unreal.log('PARAMETRIC_PLATFORM_RENDERED')
            stop()
        except Exception:
            stop()
            raise
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle'] = unreal.register_slate_post_tick_callback(tick)
