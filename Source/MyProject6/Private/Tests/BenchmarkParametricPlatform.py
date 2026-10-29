"""Repeatable editor rebuild measurements; includes mesh, materials and collision, not GPU FPS."""
import json
import statistics
import time
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'ParametricPlatform' / 'Optimization'
out.mkdir(parents=True, exist_ok=True)
tag = 'Before' if '-platformbenchmarkbefore' in unreal.SystemLibrary.get_command_line().lower() else 'After'
unreal.EditorLoadingAndSavingUtils.load_map('/Game/GameplayActors/Platforms/ParametricPlatform/Maps/L_ParametricPlatformExamples')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
results = []
for actor in sorted(actors.get_all_level_actors(), key=lambda a: a.get_actor_label()):
    if not isinstance(actor, unreal.ParametricPlatform):
        continue
    actor.rebuild_platform()
    samples = []
    for _ in range(7):
        start = time.perf_counter()
        actor.rebuild_platform()
        samples.append((time.perf_counter() - start) * 1000)
    meshes = [c.get_dynamic_mesh() for c in actor.get_components_by_class(unreal.DynamicMeshComponent)]
    results.append({'label': actor.get_actor_label(), 'triangles': sum(m.get_triangle_count() for m in meshes),
                    'rebuild_ms_median': statistics.median(samples), 'rebuild_ms_samples': samples})
(out / (tag + '.json')).write_text(json.dumps(results, indent=2))
unreal.log('PARAMETRIC_PLATFORM_BENCHMARK_' + tag.upper() + ' ' + json.dumps(results))
