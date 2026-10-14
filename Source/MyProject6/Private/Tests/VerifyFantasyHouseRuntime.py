"""Verify loaded and newly spawned game-world actors; never saves the test world."""
import json
import time
from pathlib import Path
import unreal

unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/FantasyHouse/Lvl_FantasyHousePreview')
out = Path(unreal.Paths.project_saved_dir()) / 'FantasyHouse/Preview'
started = time.monotonic()

def tick(delta):
    if time.monotonic()-started < 5: return
    try:
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            if time.monotonic()-started > 50: raise RuntimeError('PIE timed out')
            return
        placed = unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ParametricFantasyHouse)
        assert len(placed)==1
        assert placed[0].house_mesh.get_dynamic_mesh().get_triangle_count()==8502
        unreal.SystemLibrary.execute_console_command(game,'EnableCheats')
        unreal.SystemLibrary.execute_console_command(game,'Summon /Game/Structures/FantasyHouse/BP_ParametricFantasyHouse.BP_ParametricFantasyHouse_C')
        spawned_actors = unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ParametricFantasyHouse)
        assert len(spawned_actors)==2, 'Summon did not create a game-world actor'
        spawned = next(a for a in spawned_actors if a != placed[0])
        assert spawned.house_mesh.get_dynamic_mesh().get_triangle_count()==8502
        assert spawned.stair_step_count == 28
        assert not spawned.is_actor_tick_enabled()
        assert isinstance(spawned.house_mesh.get_material(0),unreal.MaterialInstanceDynamic)
        settings = spawned.get_editor_property('body')
        settings.set_editor_property('floors',2)
        spawned.set_editor_property('body',settings)
        spawned.rebuild_house()
        assert 0 < spawned.triangle_count < 8502
        (out/'Runtime.json').write_text(json.dumps({'loaded_actor':True,'runtime_spawn':True,
            'runtime_parameter_rebuild':True,'tick_enabled':False,'default_triangles':8502},indent=2))
        unreal.log('FANTASY_HOUSE_RUNTIME_OK')
    finally:
        if game:
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
