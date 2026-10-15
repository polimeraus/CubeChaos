"""Render saved ride assets in Unreal, including a second motion phase and unlit bulbs."""
from pathlib import Path
import time,json,traceback,unreal
out=Path(unreal.Paths.project_saved_dir())/'BallerinaRide/Preview';out.mkdir(parents=True,exist_ok=True)
state={'index':0,'captures':0,'export':False,'next':time.monotonic()+12}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
try:
    world=unreal.EditorLoadingAndSavingUtils.load_map('/Game/GameplayActors/BallerinaRide/Maps/L_BallerinaRide_Demo')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ride=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricBallerinaRide))
    assert ride.get_generated_seat_count()==24,'Saved demo must retain its child seats after reload'
    assert ride.get_generated_bulb_count()==528,'Saved demo must retain its bulb instances after reload'
    assert ride.skirt.get_material(0),'Saved demo must retain its skirt material after reload'
    ride.rebuild_ride()
    lights=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.DirectionalLight)]
    for i,light in enumerate(lights):
        light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property('cast_shadows',i==0)
        light.light_component.set_intensity([5,2,1][i%3])
        light.set_actor_rotation([unreal.Rotator(-45,145,0),unreal.Rotator(-25,-35,0),unreal.Rotator(-60,45,0)][i%3],False)
    skies=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.SkyLight)]
    sky=skies[0] if skies else actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,1000))
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.light_component.set_intensity(4)
    fills=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.PointLight)]
    fill=fills[0] if fills else actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(1100,-700,1150))
    fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    fill.light_component.set_editor_property('intensity_units',unreal.LightUnits.CANDELAS)
    fill.light_component.set_editor_property('attenuation_radius',5000)
    fill.light_component.set_editor_property('cast_shadows',False)
    fill.light_component.set_intensity(1000)
    camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(2000,-2000,1400))
    capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    capture.set_editor_property('capture_every_frame',False)
    capture.set_editor_property('capture_on_movement',False)
    capture.set_editor_property('always_persist_rendering_state',True)
    capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    capture.set_editor_property('fov_angle',45)
    post=capture.get_editor_property('post_process_settings')
    for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,'override_auto_exposure_bias':True,'auto_exposure_bias':0,'override_bloom_intensity':True,'bloom_intensity':.35}.items():post.set_editor_property(key,value)
    capture.set_editor_property('post_process_settings',post)
    target=unreal.RenderingLibrary.create_render_target2d(world,1440,1100,unreal.TextureRenderTargetFormat.RTF_RGBA8)
    capture.set_editor_property('texture_target',target)
    views=[('Whole',unreal.Vector(2150,-2150,1620),unreal.Vector(0,0,310),0),
           ('Wave',unreal.Vector(2150,-2150,1620),unreal.Vector(0,0,310),5),
           ('Figure',unreal.Vector(1030,-420,730),unreal.Vector(0,0,590),0),
           ('Normals',unreal.Vector(1030,-420,730),unreal.Vector(0,0,590),0),
           ('Seats',unreal.Vector(1390,-820,810),unreal.Vector(420,-240,255),0),
           ('Night',unreal.Vector(2150,-2150,1620),unreal.Vector(0,0,310),0)]
    def finish(error=None):
        if error:state['error']=error
        (out/'Report.json').write_text(json.dumps(state,default=str,indent=2))
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')
    def tick(delta):
        if time.monotonic()<state['next']:return
        try:
            if state['captures']:
                capture.capture_scene();state['captures']-=1
                state.update(export=state['captures']==0,next=time.monotonic()+.4);return
            if state['export']:
                unreal.RenderingLibrary.export_render_target(world,target,str(out),views[state['index']][0]+'.png')
                state['index']+=1;state['export']=False
                if state['index']==len(views):unreal.log('BALLERINA_RENDER_OK');finish();return
            name,location,look,seconds=views[state['index']]
            capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_NORMAL if name=='Normals' else unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            ride.set_motion_time(seconds)
            if name=='Night':
                fill.light_component.set_intensity(30)
                sky.light_component.set_intensity(.08)
                for light in lights:light.light_component.set_intensity(.15)
                ride.set_editor_property('chase_lights',False);ride.set_motion_time(0)
            camera.set_actor_location(location,False,False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,look),False)
            state.update(captures=5,next=time.monotonic()+1)
        except Exception:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
except Exception:
    (out/'Report.json').write_text(json.dumps({'error':traceback.format_exc()}))
    unreal.SystemLibrary.execute_console_command(None,'QUIT_EDITOR')
