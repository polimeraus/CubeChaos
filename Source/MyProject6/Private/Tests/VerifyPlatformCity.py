"""Fresh process: verify saved assets and ownership, regenerate, export actual outlines; optional RHI render."""
import json
import time
from pathlib import Path
import unreal

ROOT = '/Game/Procedural/PlatformCity'
RULE_FIELDS = ('width', 'height', 'vertical_roads', 'horizontal_roads', 'main_road_width',
    'district_variation', 'road_curvature', 'side_road_width', 'side_road_bend', 'branch_chance',
    'min_plate_size', 'target_plate_size', 'large_plate_chance', 'thickness', 'corner_radius',
    'corner_segments', 'max_plates', 'road_sample_spacing')
def rules_values(actor):
    return {key: actor.rules.get_editor_property(key) for key in RULE_FIELDS}

out = Path(unreal.Paths.project_saved_dir()) / 'PlatformCity'
expected = json.loads((out / 'Created.json').read_text())
blueprint = unreal.load_asset(ROOT + '/BP_ParametricPlatformCity')
assert blueprint
defaults = unreal.get_default_object(blueprint.generated_class())
assert defaults.get_editor_property('randomize_on_begin_play')
assert defaults.get_editor_property('generate_on_begin_play')
assert not (Path(unreal.Paths.project_content_dir()) / 'CityLayout/Rules/DefaultCityLayout.ini').exists()
assert defaults.rules.width == expected['width']
assert defaults.rules.corner_radius == expected['corner_radius']
native = unreal.get_default_object(unreal.ParametricPlatformCity)
assert rules_values(defaults) == rules_values(native), (rules_values(defaults), rules_values(native))
world = unreal.EditorLoadingAndSavingUtils.load_map(ROOT + '/Maps/L_PlatformCity_Demo')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
city = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricPlatformCity))
assert city.seed == expected['seed']
assert city.generated_plan.signature == expected['signature']
assert len(city.get_platforms()) == expected['platform_count']
assert rules_values(city) == rules_values(defaults), rules_values(city)

def inspect():
    plates = list(city.get_platforms())
    actual = [a for a in actors.get_all_level_actors() if a.get_owner() == city]
    assert len(actual) == len(plates), 'Duplicate or untracked plates'
    records = []
    for plate in plates:
        assert isinstance(plate, unreal.ParametricPlatform)
        assert plate.get_platform_shape() == unreal.ChunkedDecorativeFloorShape.DYNAMIC_POLYGON
        assert len(plate.polygon_outline) >= 3
        assert plate.get_attach_parent_actor() == city
        assert plate.get_platform_mesh_count() == 1
        assert plate.get_destructible_cell_count() == 0
        assert not plate.shape_warning, plate.shape_warning
        meshes = plate.get_components_by_class(unreal.DynamicMeshComponent)
        assert any(c.get_dynamic_mesh().get_triangle_count() > 0 for c in meshes), 'Missing restored geometry'
        p = plate.get_actor_location()
        records.append({'label': plate.get_actor_label(), 'shape': str(plate.get_platform_shape()),
            'center': [p.x, p.y, p.z], 'outline': [[v.x + p.x, v.y + p.y] for v in plate.get_platform_outline()]})
    return {'seed': city.seed, 'signature': city.generated_plan.signature, 'plates': records,
            'roads': [{'kind': str(r.kind), 'width': r.width, 'points': [[p.x,p.y] for p in r.points],
                       'outline': [[p.x,p.y] for p in r.outline]} for r in city.generated_plan.roads]}

saved = inspect()
assert city.generate_with_seed(expected['seed']), city.last_error
assert inspect() == saved, 'Reloaded preview differs from a deterministic regeneration'
layouts = [saved]
for seed in [5821, 97631]:
    assert city.generate_with_seed(seed), city.last_error
    layouts.append(inspect())
fresh = actors.spawn_actor_from_class(blueprint.generated_class(), unreal.Vector(20000, 0, 0))
assert fresh.get_platforms() and not fresh.last_error
assert fresh.get_editor_property('randomize_on_begin_play')
assert rules_values(fresh) == rules_values(defaults), rules_values(fresh)
# A Details-panel property change rebuilds automatically and needs no external text file.
serial = fresh.generation_serial
edited = fresh.get_editor_property('rules')
edited.set_editor_property('width', 9200.)
edited.set_editor_property('corner_radius', 150.)
fresh.set_editor_property('rules', edited)
assert fresh.generation_serial > serial and fresh.applied_rules.width == 9200., (
    serial, fresh.generation_serial, rules_values(fresh), fresh.applied_rules.width, fresh.last_error)
assert fresh.applied_rules.corner_radius == 150. and not fresh.last_error
actors.destroy_actor(fresh)
assert city.generate_with_seed(expected['seed'])
(out / 'Persistence.json').write_text(json.dumps({'passed': True, 'fresh_process': True, 'layouts': layouts}, indent=2))
unreal.log('PLATFORM_CITY_PERSISTENCE_OK')

if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
    location = unreal.Vector(0, -4700, 6800)
    camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, location,
        unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector()))
    capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for key, value in {'capture_every_frame': False, 'capture_on_movement': False,
                       'always_persist_rendering_state': True, 'fov_angle': 62,
                       'capture_source': unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():
        capture.set_editor_property(key, value)
    post = capture.get_editor_property('post_process_settings')
    for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
                       'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
                       'override_auto_exposure_bias': True, 'auto_exposure_bias': .5}.items():
        post.set_editor_property(key, value)
    capture.set_editor_property('post_process_settings', post)
    target = unreal.RenderingLibrary.create_render_target2d(world, 1600, 1100, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    capture.set_editor_property('texture_target', target)
    state = {'next': time.monotonic() + 12, 'captures': 0}
    def tick(_):
        if time.monotonic() < state['next']:
            return
        try:
            if state['captures'] < 4:
                capture.capture_scene()
                state.update(captures=state['captures'] + 1, next=time.monotonic() + 1)
                return
            unreal.RenderingLibrary.export_render_target(world, target, str(out), 'PlatformCity.png')
            unreal.log('PLATFORM_CITY_RENDER_OK')
        finally:
            if state['captures'] >= 4 and time.monotonic() >= state['next']:
                unreal.unregister_slate_post_tick_callback(state['handle'])
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle'] = unreal.register_slate_post_tick_callback(tick)
