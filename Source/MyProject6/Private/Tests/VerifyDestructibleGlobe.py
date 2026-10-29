"""Load saved assets in a fresh editor process and render the intact, shaft, damaged and variant views."""
import json
import time
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/Globe'
out = Path(unreal.Paths.project_saved_dir()) / 'Globe/Preview'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map(folder + '/Maps/Lvl_DestructibleGlobePreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(folder + '/BP_ChunkedDestructibleGlobe')
globe = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleGlobe))
cdo = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ChunkedDestructibleGlobe)
keys = ('meridian_count', 'parallel_count', 'sphere_radius', 'shaft_width', 'meridian_thickness', 'parallel_thickness', 'curve_segments',
        'piece_size', 'piece_gap', 'panel_material', 'edge_material', 'enable_launch_pad', 'pad_diameter',
        'auto_launch_height', 'height_above_top', 'launch_up_speed', 'launch_np_cs', 'launch_pad_material')
def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))

original = triangles(globe)
for cls in (bp.generated_class(), unreal.ChunkedDestructibleGlobe):
    fresh = actors.spawn_actor_from_class(cls, unreal.Vector(20000,0,0))
    for key in keys:
        assert fresh.get_editor_property(key) == globe.get_editor_property(key) == cdo.get_editor_property(key) == native.get_editor_property(key), key
    assert triangles(fresh) == original and not fresh.generation_warning
    pad = fresh.get_editor_property('ground_launch_pad').get_editor_property('child_actor')
    assert isinstance(pad, unreal.LaunchPad) and pad.get_editor_property('use_target_height')
    assert pad.get_editor_property('shape') == unreal.LaunchPadShape.CIRCLE
    actors.destroy_actor(fresh)
assert original == globe.generated_piece_count * 12
results = {'fresh_process_persistence': True, 'native_blueprint_placed_and_new_instances_match': True,
           'pieces': globe.generated_piece_count, 'triangles': original, 'height_cm': globe.generated_height,
           'reference_defaults': {key: globe.get_editor_property(key) for key in
                                  ('meridian_count', 'parallel_count', 'shaft_width', 'meridian_thickness', 'parallel_thickness')}}
(out/'Persistence.json').write_text(json.dumps(results, indent=2))

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector())
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in {'capture_every_frame': False, 'capture_on_movement': False, 'always_persist_rendering_state': True,
                   'fov_angle': 28., 'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(key, value)
post = capture.get_editor_property('post_process_settings')
for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                   'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                   'override_auto_exposure_bias': True, 'auto_exposure_bias': .1}.items():
    post.set_editor_property(key, value)
capture.set_editor_property('post_process_settings', post)
target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1200, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
views = ['Reference', 'Shaft', 'Damage', 'Variant']
state = {'next': time.monotonic()+12, 'index': 0, 'captures': 0, 'export': False}

def stop():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(dt):
    if time.monotonic() < state['next']: return
    try:
        if state['captures']:
            capture.capture_scene(); state['captures'] -= 1
            state.update(export=state['captures']==0, next=time.monotonic()+.5)
            return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world, target, str(out), views[state['index']]+'.png')
            state['index'] += 1; state['export'] = False
            if state['index'] == len(views):
                (out/'Verification.json').write_text(json.dumps(results, indent=2))
                unreal.log('DESTRUCTIBLE_GLOBE_VERIFIED ' + str(results)); stop(); return
        name = views[state['index']]
        location, look, fov = unreal.Vector(4800,-6800,6300), unreal.Vector(0,0,1550), 26.
        if name == 'Shaft':
            location, look, fov = unreal.Vector(80,-100,3550), unreal.Vector(0,0,0), 65.
        elif name == 'Damage':
            for key, value in {'flash_duration': 0., 'play_destroy_tick_sound': False, 'use_destruction_ripple': False}.items():
                globe.set_editor_property(key, value)
            removed = globe.destroy_cells_in_radius(unreal.Vector(650,-1125,1550), 650, unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed > 10 and triangles(globe) < original
            results['blast_removed_pieces'] = removed
        elif name == 'Variant':
            globe.set_editor_property('meridian_count', 8); globe.set_editor_property('parallel_count', 3)
            globe.rebuild_globe()
            assert not globe.generation_warning and globe.generated_piece_count != results['pieces']
            results['variant_pieces'] = globe.generated_piece_count
        camera.set_actor_location(location, False, False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, look), False)
        capture.set_editor_property('fov_angle', fov)
        state.update(captures=5, next=time.monotonic()+2)
    except Exception:
        stop(); raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
