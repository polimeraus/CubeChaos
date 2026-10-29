"""Play the saved example with the project's actual FirstPerson GameMode. No saves."""
import json
import time
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'ProceduralCity'
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Procedural/CityGenerator/Maps/Lvl_City_Radial')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
source=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ProceduralCityGenerator))
saved_seed=source.seed
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state={'start':time.monotonic(),'busy':False,'phase':0,'next':time.monotonic()+5}
report={}
def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    report.update(passed=error is None,error=error)
    (out/'Runtime.json').write_text(json.dumps(report,indent=2))
    if error:unreal.log_error('CITY_RUNTIME_FAILED '+error)
    else:unreal.log('PROCEDURAL_CITY_RUNTIME_OK')
    level.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(dt):
    if state['busy'] or time.monotonic()<state['next']:return
    state['busy']=True
    try:
        assert time.monotonic()-state['start']<120,'PIE timeout'
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:return
        cities=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.ProceduralCityGenerator)
        if not cities:return
        city=cities[0]
        pawn=unreal.GameplayStatics.get_player_pawn(world,0)
        assert pawn,'FirstPerson player was not spawned'
        assert not city.last_error,city.last_error
        owned=list(city.get_generated_actors())
        actual=[a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor) if a.get_owner()==city and a.actor_has_tag('ProceduralCity.Generated')]
        assert len(owned)==len(actual)==city.generated_actor_count and len(owned)>20
        assert not city.is_actor_tick_enabled()
        entry=city.get_entry_transform().translation
        p=pawn.get_actor_location()
        assert (p-entry).length()<200,'Player is not standing at the entry'
        if state['phase']==0:
            assert city.seed!=saved_seed,'BeginPlay did not generate a new seed'
            report.update(begin_play_seed=city.seed,saved_seed=saved_seed,begin_play_actors=len(owned),player_class=pawn.get_class().get_path_name())
            assert city.generate_with_seed(20261028),city.last_error
            assert city.generated_plan.signature=='CITY-V1-D4C6A924'
            state.update(phase=1,next=time.monotonic()+3)
        else:
            report.update(runtime_rebuild_signature=city.generated_plan.signature,runtime_rebuild_actors=len(owned),player_entry_supported=True,no_duplicate_actors=True)
            finish()
    except Exception as error:finish(str(error))
    finally:state['busy']=False
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
