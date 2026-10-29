"""Fresh-process saved asset, native/BP spawn, rendering, destruction and runtime checks. No asset writes."""
import json
import time
from pathlib import Path
import unreal

ROOT='/Game/Structures/Destructible/MayaPyramid'
out=Path(unreal.Paths.project_saved_dir())/'MayaPyramid/Architecture'
out.mkdir(parents=True,exist_ok=True)
world=unreal.EditorLoadingAndSavingUtils.load_map(ROOT+'/Maps/Lvl_MayaPyramidPreview')
assert world
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp=unreal.load_asset(ROOT+'/BP_DestructibleMayaPyramid')
pyramid=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleMayaPyramid))
cdo=unreal.get_default_object(bp.generated_class())
native=unreal.get_default_object(unreal.ChunkedDestructibleMayaPyramid)
keys=('base_width','tier_count','tier_height','summit_width_ratio','piece_size','stair_width_ratio',
      'side_staircases','podium_height','podium_width_ratio','slab_thickness',
      'window_bay_width','animate_interior_lighting','interior_brightness','lighting_cycle_seconds','wall_material','ledge_material','accent_material',
      'stair_material','shrine_material','crest_material','gold_material','step_material')

def triangles(actor):
    return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent))

original=triangles(pyramid)
for cls in (bp.generated_class(),unreal.ChunkedDestructibleMayaPyramid):
    fresh=actors.spawn_actor_from_class(cls,unreal.Vector(20000,0,0))
    for key in keys:
        assert fresh.get_editor_property(key)==pyramid.get_editor_property(key)==cdo.get_editor_property(key)==native.get_editor_property(key),key
    assert triangles(fresh)==original and not fresh.generation_warning
    actors.destroy_actor(fresh)
assert original==pyramid.generated_piece_count*12
for key in keys[-8:]:
    material=pyramid.get_editor_property(key)
    assert material and material.get_path_name().startswith(ROOT+'/Materials/')
for comp in pyramid.get_components_by_class(unreal.DynamicMeshComponent):
    assert comp.get_num_materials()==8 and all(comp.get_material(i) for i in range(8))
results={'fresh_process_persistence':True,'native_blueprint_saved_and_new_instances_match':True,
         'pieces':pyramid.generated_piece_count,'triangles':original,'height_cm':pyramid.generated_height,
         'new_materials':[pyramid.get_editor_property(key).get_path_name() for key in keys[-8:]]}
(out/'Persistence.json').write_text(json.dumps(results,indent=2))


# Load each migrated level in this fresh process and verify saved instances have the same class settings.
setup=json.loads((out/'Setup.json').read_text())
for map_path in sorted({row['map'] for row in setup['instances']}):
    unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    for instance in actors.get_all_level_actors():
        if isinstance(instance,unreal.ChunkedDestructibleMayaPyramid):
            for key in keys:
                assert instance.get_editor_property(key)==cdo.get_editor_property(key),(map_path,instance.get_actor_label(),key)
            assert triangles(instance)==original and not instance.generation_warning
results['migrated_levels_verified']=len({row['map'] for row in setup['instances']})
unreal.EditorLoadingAndSavingUtils.load_map(ROOT+'/Maps/Lvl_MayaPyramidPreview')
state={'start':time.monotonic()}

def stop(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    results['passed']=error is None
    if error:results['error']=error;unreal.log_error('MAYA_ARCHITECTURE_VERIFY_FAILED '+error)
    else:unreal.log('MAYA_ARCHITECTURE_VERIFIED '+json.dumps(results))
    (out/'Verification.json').write_text(json.dumps(results,indent=2))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_dt):
    try:
        assert time.monotonic()-state['start']<120,'Runtime verification timed out'
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleMayaPyramid)
        assert len(runtime)==1 and triangles(runtime[0])==original
        placed=runtime[0]
        placed.set_interior_lighting(.15,False)
        assert triangles(placed)==original and not placed.is_actor_tick_enabled()
        unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
        unreal.SystemLibrary.execute_console_command(game,'Summon '+bp.generated_class().get_path_name())
        runtime=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChunkedDestructibleMayaPyramid)
        assert len(runtime)==2
        spawned=next(a for a in runtime if a!=placed)
        assert triangles(spawned)==original and spawned.animate_interior_lighting and not spawned.is_actor_tick_enabled()
        for key,value in {'flash_duration':0.,'play_destroy_tick_sound':False,'use_destruction_ripple':False}.items():spawned.set_editor_property(key,value)
        impact=unreal.MathLibrary.transform_location(spawned.get_actor_transform(),unreal.Vector(1050,-1900,750))
        removed=spawned.destroy_cells_in_radius(impact,500,unreal.ChunkedFloorBlastShape.SPHERE)
        assert removed>10 and triangles(spawned)==original-removed*12
        spawned.set_interior_lighting(1.2,True)
        assert triangles(spawned)==original-removed*12
        spawned.set_editor_property('tier_count',5)
        spawned.rebuild_maya_pyramid()
        assert triangles(spawned)!=original and not spawned.generation_warning
        results.update(runtime_spawn=True,runtime_rebuild=True,lighting_preserves_damage=True,
                       explosion_removed_pieces=removed,idle_tick_disabled=True,pie_begin_play=True)
        stop()
    except Exception as error:stop(str(error))
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
