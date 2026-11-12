"""Two independent PIE starts of the saved demo must produce two distinct random layouts."""
import json
import time
import traceback
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'PlatformCity'
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Procedural/PlatformCity/Maps/L_PlatformCity_Demo')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
source = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.ParametricPlatformCity))
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state = {'start': time.monotonic(), 'next': time.monotonic()+4, 'phase': 'first', 'busy': False}
report = {'saved_seed': source.seed, 'runs': []}
RULE_FIELDS = ('width', 'height', 'vertical_roads', 'horizontal_roads', 'main_road_width',
    'district_variation', 'road_curvature', 'side_road_width', 'side_road_bend', 'branch_chance',
    'min_plate_size', 'target_plate_size', 'large_plate_chance', 'thickness', 'corner_radius',
    'corner_segments', 'max_plates', 'road_sample_spacing')

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    report.update(passed=error is None, error=error)
    (out / 'Runtime.json').write_text(json.dumps(report, indent=2))
    (unreal.log_error if error is not None else unreal.log)('PLATFORM_CITY_RUNTIME_' + ('FAILED ' + str(error) if error is not None else 'OK'))
    level.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_):
    if state['busy'] or time.monotonic() < state['next']:
        return
    state['busy'] = True
    try:
        assert time.monotonic()-state['start'] < 120, 'PIE timeout'
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if state['phase'] == 'between':
            if game:
                return
            state.update(phase='second', next=time.monotonic()+4)
            level.editor_request_begin_play()
            return
        if not game:
            return
        cities = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.ParametricPlatformCity)
        if not cities:
            return
        city = cities[0]
        assert not city.last_error, city.last_error
        assert city.seed != report['saved_seed']
        assert sum(r.kind == unreal.PlatformCityRoadKind.VERTICAL_MAIN for r in city.generated_plan.roads) == 3
        assert sum(r.kind == unreal.PlatformCityRoadKind.HORIZONTAL_MAIN for r in city.generated_plan.roads) == 2
        assert all(city.rules.get_editor_property(key) == source.rules.get_editor_property(key)
                   for key in RULE_FIELDS), 'PIE changed saved layout properties'
        actual = [a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.ParametricPlatform) if a.get_owner() == city]
        assert len(actual) == len(city.get_platforms()) == len(city.generated_plan.plots), 'Duplicated saved preview'
        assert all(a.get_platform_mesh_count() == 1 for a in actual)
        assert all(a.get_platform_shape() == unreal.ChunkedDecorativeFloorShape.DYNAMIC_POLYGON for a in actual)
        report['runs'].append({'seed': city.seed, 'signature': city.generated_plan.signature, 'plates': len(actual)})
        if state['phase'] == 'first':
            state.update(phase='between', next=time.monotonic()+3)
            level.editor_request_end_play()
        else:
            assert report['runs'][0]['seed'] != city.seed
            assert report['runs'][0]['signature'] != city.generated_plan.signature
            assert source.seed == report['saved_seed'], 'PIE mutated editor preview'
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        state['busy'] = False

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle'] = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
