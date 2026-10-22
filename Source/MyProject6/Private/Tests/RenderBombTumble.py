"""Capture the real PIE bomb impulse on a living NPC, including its landing."""
import json, time, traceback
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'BombRetro/Tumble'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
npc=actors.spawn_actor_from_class(unreal.ChasingNPC,unreal.Vector(180,0,100))
npc.set_editor_property('spawn_replacements_on_death',False)
npc.set_editor_property('contact_energy_drain_per_second',0.)
bomb=actors.spawn_actor_from_class(unreal.BombProjectile,unreal.Vector(0,0,60))
bomb.set_editor_property('blast_damage',0.)
bomb.set_editor_property('player_energy_damage',0.)
bomb.set_editor_property('launch_velocity',unreal.Vector())
bomb.set_editor_property('fuse_seconds',100.)
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(500,300,.2))
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500))
sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
sky.light_component.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
sky.light_component.set_editor_property('intensity',2.)
for rot,intensity in [(unreal.Rotator(pitch=-45,yaw=-30),5.),(unreal.Rotator(pitch=-25,yaw=150),2.)]:
    lamp=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1000),rot)
    lamp.light_component.set_editor_property('intensity',intensity)
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(600,-750,350))
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
target=unreal.RenderingLibrary.create_render_target2d(world,800,800,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
capture.set_editor_property('capture_every_frame',True)
capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width',420.)
post=capture.get_editor_property('post_process_settings')
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
            'override_auto_exposure_bias':True,'auto_exposure_bias':10.}.items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
state={'stage':'start','since':time.monotonic(),'index':0,'rows':[]}
frames=[('Before',0.),('Air_015',.15),('Air_035',.35),('Air_065',.65),('Air_100',1.),('Landed',4.)]

def close(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    (out/'Result.json').write_text(json.dumps({'error':error,'frames':state['rows']},indent=2))
    level.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.log('BOMB_TUMBLE_RENDER '+('FAILED '+error if error else 'OK'))

def tick(delta):
    try:
        now=time.monotonic()
        if state['stage']=='start' and now-state['since']>3:
            level.editor_request_begin_play();state['stage']='world'
        elif state['stage']=='world':
            game=editor.get_game_world()
            if not game:return
            npcs=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChasingNPC)
            cams=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.SceneCapture2D)
            bombs=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.BombProjectile)
            if not npcs or not cams or not bombs:return
            n=npcs[0]
            if n.get_controller():n.get_controller().destroy_actor()
            n.set_actor_location(unreal.Vector(180,0,100),False,True)
            n.character_movement.set_editor_property('run_physics_with_no_controller',True)
            player=unreal.GameplayStatics.get_player_character(game,0)
            if player:player.set_actor_location(unreal.Vector(-5000,0,100),False,True)
            state.update(game=game,npc=n,camera=cams[0],bomb=bombs[0],stage='warm',since=now)
        elif state['stage'] in ['warm','capture']:
            n=state['npc'];cam=state['camera'];game=state['game']
            pos=n.get_actor_location()
            eye=pos+unreal.Vector(500,-750,230)
            cam.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,pos),False,False)
            cap=cam.get_component_by_class(unreal.SceneCaptureComponent2D)
            if state['stage']=='warm':
                if now-state['since']<5:return
                state['started']=unreal.GameplayStatics.get_time_seconds(game)
                state['stage']='capture'
            elapsed=unreal.GameplayStatics.get_time_seconds(game)-state['started']
            name,deadline=frames[state['index']]
            if elapsed<deadline:return
            cap.capture_scene()
            unreal.RenderingLibrary.export_render_target(game,cap.texture_target,str(out),name+'.png')
            state['rows'].append({'name':name,'time':elapsed,'position':str(pos),'mesh_rotation':str(n.mesh.get_relative_transform().rotation)})
            if state['index']==0:
                state['bomb'].set_actor_location(unreal.Vector(0,0,90),False,True)
                state['bomb'].explode()
            state['index']+=1
            if state['index']==len(frames):close()
    except Exception:close(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
