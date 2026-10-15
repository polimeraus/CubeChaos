"""Render the polygon temple, the destructible raised spiral rim and the aligned launch shaft."""
import json,time
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir())/'StructureUpgrade/Preview';out.mkdir(parents=True,exist_ok=True)
world=unreal.EditorLoadingAndSavingUtils.load_map('/Game/Structures/PolygonTower/Lvl_PolygonTowerPreview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tower=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ChunkedDestructiblePolygonTower))
cone=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.QuadPanelConeStructure))
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for k,v in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,
            'fov_angle':40.,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():capture.set_editor_property(k,v)
post=capture.get_editor_property('post_process_settings')
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
            'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
            'override_auto_exposure_bias':True,'auto_exposure_bias':.2,
            'override_ambient_occlusion_intensity':True,'ambient_occlusion_intensity':.8}.items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
target=unreal.RenderingLibrary.create_render_target2d(world,1400,1200,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
views=[('Polygon', (4200,-9300,2800),(0,0,1850)),
       ('SpiralRim',(-1500,-7000,2900),(-4500,0,1700)),
       ('RimDetail',(-2800,-2800,1700),(-4500,-600,700)),
       ('Shaft',(1,1,4400),(1,1,100)),
       ('Damage',(4200,-9300,2800),(0,0,1850))]
state={'next':time.monotonic()+20,'index':0,'captures':0,'export':False}
def stop():
    unreal.unregister_slate_post_tick_callback(state['handle']);unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(dt):
    if time.monotonic()<state['next']:return
    try:
        if state['captures']:
            capture.capture_scene();state['captures']-=1
            state.update(export=state['captures']==0,next=time.monotonic()+.4);return
        if state['export']:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),views[state['index']][0]+'.png')
            state['index']+=1;state['export']=False
            if state['index']==len(views):unreal.log('STRUCTURE_UPGRADE_RENDERED');stop();return
        name,location,look=views[state['index']]
        if name=='Damage':
            tower.set_editor_property('flash_duration',0.);tower.set_editor_property('play_destroy_tick_sound',False)
            tower.set_editor_property('use_destruction_ripple',False)
            removed=tower.destroy_cells_in_radius(unreal.Vector(500,-1600,500),400,unreal.ChunkedFloorBlastShape.CIRCLE)
            assert removed>5
        camera.set_actor_location(unreal.Vector(*location),False,False)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*location),unreal.Vector(*look)),False)
        state.update(captures=4,next=time.monotonic()+2)
    except Exception:
        stop();raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
