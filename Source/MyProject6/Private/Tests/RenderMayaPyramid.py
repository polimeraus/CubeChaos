"""Render the revised architecture, lighting extremes and damage through the actual viewport."""
import json
import time
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'MayaPyramid/Architecture'
out.mkdir(parents=True,exist_ok=True)
world=unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/Destructible/MayaPyramid/Maps/Lvl_MayaPyramidPreview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
pyramid=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructibleMayaPyramid))
for cmd in ('ShowFlag.PostProcessing 1','ShowFlag.Bloom 1','ShowFlag.Tonemapper 1','ShowFlag.AmbientOcclusion 1',
            'r.BloomQuality 5','r.DefaultFeature.Bloom 1','r.AntiAliasingMethod 1','viewmode lit'):
    unreal.SystemLibrary.execute_console_command(world,cmd)
actors.set_selected_level_actors([])
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(),transient=True)
component=camera.get_component_by_class(unreal.CameraComponent)
component.set_editor_property('field_of_view',42.)
component.set_editor_property('constrain_aspect_ratio',False)
post_actor=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.PostProcessVolume))
component.set_editor_property('post_process_settings',post_actor.get_editor_property('settings'))
component.set_editor_property('post_process_blend_weight',1.)
views=['Reference','Front','Dark','Detail','Damage']
state={'start':time.monotonic(),'next':time.monotonic()+12,'task':None,'index':0,'prepared':False}
results={}

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    results.update(passed=error is None,error=error)
    (out/'Render.json').write_text(json.dumps(results,indent=2))
    if error:unreal.log_error('MAYA_ARCHITECTURE_RENDER_FAILED '+error)
    else:unreal.log('MAYA_ARCHITECTURE_RENDERED')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(_dt):
    if time.monotonic()<state['next']:return
    try:
        assert time.monotonic()-state['start']<240,'Render timed out'
        if state['task']:
            if not state['task'].is_task_done():return
            state.update(task=None,index=state['index']+1,prepared=False)
            if state['index']==len(views):finish();return
        name=views[state['index']]
        if not state['prepared']:
            location,look,fov=unreal.Vector(7000,-9600,5900),unreal.Vector(0,0,1250),42.
            pyramid.set_interior_lighting(.04 if name=='Dark' else 1.,False)
            if name=='Front':location=unreal.Vector(0,-11000,3900)
            if name=='Detail':location,look,fov=unreal.Vector(4000,-6200,2300),unreal.Vector(850,-1600,1000),34.
            if name=='Damage':
                for key,value in {'flash_duration':0.,'play_destroy_tick_sound':False,'use_destruction_ripple':False}.items():pyramid.set_editor_property(key,value)
                results['removed_pieces']=pyramid.destroy_cells_in_radius(unreal.Vector(1250,-1900,850),650,unreal.ChunkedFloorBlastShape.SPHERE)
                assert results['removed_pieces']>10
            camera.set_actor_location(location,False,False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
            component.set_editor_property('field_of_view',fov)
            state.update(prepared=True,next=time.monotonic()+3)
            return
        state['task']=unreal.AutomationLibrary.take_high_res_screenshot(1440,1200,str(out/(name+'.png')),camera,False,False)
        assert state['task'] and state['task'].is_valid_task()
    except Exception as error:finish(str(error))

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
