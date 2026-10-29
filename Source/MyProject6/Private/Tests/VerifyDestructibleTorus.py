"""Fresh-process persistence checks and rendered views of the saved open torus."""
import json
import time
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/Torus'
out = Path(unreal.Paths.project_saved_dir())/'Torus/Preview'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_DestructibleTorusPreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(folder+'/BP_ChunkedDestructibleTorus')
torus = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleTorus))
cdo = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ChunkedDestructibleTorus)
keys = ('ring_radius', 'tube_radius', 'opening_angle', 'opening_direction', 'rounded_ends',
        'meridian_count', 'parallel_count', 'meridian_thickness', 'parallel_thickness',
        'curve_segments', 'ring_segments', 'piece_size', 'piece_gap', 'panel_material', 'edge_material')

def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))

original = triangles(torus)
for cls in (bp.generated_class(), unreal.ChunkedDestructibleTorus):
    fresh = actors.spawn_actor_from_class(cls, unreal.Vector(20000,0,0))
    for key in keys:
        assert fresh.get_editor_property(key) == torus.get_editor_property(key) == cdo.get_editor_property(key) == native.get_editor_property(key), key
    assert triangles(fresh) == original and not fresh.generation_warning
    actors.destroy_actor(fresh)
assert original == torus.generated_piece_count*12
assert torus.panel_material and torus.edge_material
results = {'fresh_process_persistence': True, 'native_blueprint_placed_and_new_instances_match': True,
           'pieces': torus.generated_piece_count, 'triangles': original,
           'height_cm': torus.generated_height, 'center_opening_width_cm': torus.center_opening_width,
           'reference_defaults': {key: torus.get_editor_property(key) for key in keys[:-2]}}
(out/'Persistence.json').write_text(json.dumps(results, indent=2))

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector())
capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key, value in {'capture_every_frame': False, 'capture_on_movement': False, 'always_persist_rendering_state': True,
                   'fov_angle': 35., 'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
    capture.set_editor_property(key, value)
post = capture.get_editor_property('post_process_settings')
for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                   'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                   'override_auto_exposure_bias': True, 'auto_exposure_bias': .1}.items():
    post.set_editor_property(key, value)
capture.set_editor_property('post_process_settings', post)
target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
views = ['Reference', 'Top', 'Damage', 'Closed']
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
                unreal.log('DESTRUCTIBLE_TORUS_VERIFIED '+str(results)); stop(); return
        name = views[state['index']]
        location, look, fov = unreal.Vector(3000,-7800,4700), unreal.Vector(0,0,650), 35.
        if name == 'Top':
            location, look, fov = unreal.Vector(0,-1,9000), unreal.Vector(0,0,500), 48.
        elif name == 'Damage':
            for key, value in {'flash_duration': 0., 'play_destroy_tick_sound': False, 'use_destruction_ripple': False}.items():
                torus.set_editor_property(key, value)
            removed = torus.destroy_cells_in_radius(unreal.Vector(1600,-1200,750), 600, unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed > 10 and triangles(torus) < original
            results['blast_removed_pieces'] = removed
        elif name == 'Closed':
            torus.set_editor_property('opening_angle', 0.)
            torus.set_editor_property('meridian_count', 24); torus.set_editor_property('parallel_count', 5)
            torus.rebuild_torus()
            assert not torus.generation_warning and torus.generated_piece_count != results['pieces']
            results['closed_variant_pieces'] = torus.generated_piece_count
        camera.set_actor_location(location, False, False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, look), False)
        capture.set_editor_property('fov_angle', fov)
        state.update(captures=5, next=time.monotonic()+2)
    except Exception:
        stop(); raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
