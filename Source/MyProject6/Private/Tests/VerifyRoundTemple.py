"""Fresh-process persistence checks and rendered views of the saved round temple."""
import json
import time
from pathlib import Path
import unreal

folder = '/Game/Structures/Destructible/RoundTemple'
out = Path(unreal.Paths.project_saved_dir())/'RoundTemple/Preview'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map(folder+'/Maps/Lvl_DestructibleRoundTemplePreview')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.load_asset(folder+'/BP_ChunkedDestructibleRoundTemple')
temple = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChunkedDestructibleRoundTemple))
cdo = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ChunkedDestructibleRoundTemple)
keys = ('body_radius','lower_floor_height','upper_radius_ratio','upper_floor_height','wall_thickness',
        'floor_band_height','lower_arch_count','upper_arch_count','arch_width_ratio','arch_segments',
        'rectangular_entrance','gallery_height','gallery_post_width','roof_height','roof_overhang','roof_thickness',
        'roof_courses','platform_height','platform_margin','front_steps','step_count','step_depth','stair_arc_degrees',
        'circle_segments','piece_size','piece_gap','stone_material','band_material','roof_material','step_material')

def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))

original = triangles(temple)
for cls in (bp.generated_class(), unreal.ChunkedDestructibleRoundTemple):
    fresh = actors.spawn_actor_from_class(cls, unreal.Vector(20000,0,0))
    for key in keys:
        assert fresh.get_editor_property(key) == temple.get_editor_property(key) == cdo.get_editor_property(key) == native.get_editor_property(key), key
    assert triangles(fresh) == original and not fresh.generation_warning
    actors.destroy_actor(fresh)
assert original > temple.generated_piece_count*10
assert temple.stone_material and temple.roof_material and temple.step_material
results = {'fresh_process_persistence': True, 'native_blueprint_placed_and_new_instances_match': True,
           'pieces': temple.generated_piece_count, 'triangles': original,
           'height_cm': temple.generated_height, 'roof_pieces':temple.generated_roof_pieces,'step_pieces':temple.generated_step_pieces,
           'reference_defaults': {key: temple.get_editor_property(key) for key in keys[:-4]}}
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
target = unreal.RenderingLibrary.create_render_target2d(world, 1400, 1200, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
views = ['Reference', 'Front', 'Damage']
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
                unreal.log('ROUND_TEMPLE_VERIFIED '+str(results)); stop(); return
        name = views[state['index']]
        location, look, fov = unreal.Vector(2100,-5900,2650), unreal.Vector(0,0,820), 35.
        if name == 'Front':
            location, look, fov = unreal.Vector(0,-6400,2700), unreal.Vector(0,0,820), 35.
        elif name == 'Damage':
            for key, value in {'flash_duration': 0., 'play_destroy_tick_sound': False, 'use_destruction_ripple': False}.items():
                temple.set_editor_property(key, value)
            removed = temple.destroy_cells_in_radius(unreal.Vector(300,-780,580), 340, unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed > 10 and triangles(temple) < original
            results['blast_removed_pieces'] = removed
            results['roof_blast_removed_pieces'] = temple.destroy_cells_in_radius(unreal.Vector(-180,-450,1430),220,unreal.ChunkedFloorBlastShape.SPHERE)
            assert results['roof_blast_removed_pieces'] > 0
        camera.set_actor_location(location, False, False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, look), False)
        capture.set_editor_property('fov_angle', fov)
        state.update(captures=5, next=time.monotonic()+2)
    except Exception:
        stop(); raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
