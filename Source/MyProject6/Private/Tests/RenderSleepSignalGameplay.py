"""Real PIE capture: held device, skeletal release, expanding waves, sleep and get-up."""
import json,time,traceback
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir()).resolve()/'SleepSignal/Gameplay';out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(unreal.Vector(100,100,.2))
surface=floor.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'));surface.set_vector_parameter_value('Base Color',unreal.LinearColor(.075,.09,.12,1))
npc=actors.spawn_actor_from_class(unreal.ChasingNPC,unreal.Vector(350,100,100));npc.set_editor_property('spawn_replacements_on_death',False);npc.set_editor_property('contact_energy_drain_per_second',0.);npc.set_editor_property('auto_possess_ai',unreal.AutoPossessAI.DISABLED)
for rot,intensity in [(unreal.Rotator(pitch=-45,yaw=-30),12.),(unreal.Rotator(pitch=-25,yaw=150),5.)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,600),rot);light.light_component.set_editor_property('intensity',intensity)
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500));sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.light_component.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.light_component.set_editor_property('intensity',2.)
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(0,0,170));capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('texture_target',unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8))
for k,v in {'capture_every_frame':True,'always_persist_rendering_state':True,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':90.,'enable_first_person_field_of_view':True,'first_person_field_of_view':70.,'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(k,v)
post=capture.get_editor_property('post_process_settings')
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,'override_auto_exposure_bias':True,'auto_exposure_bias':10.}.items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
frames=[('Held',0),('Windup',.16),('Release',.28),('FollowThrough',.46),('Drowsy',1.15),('Falling',1.7),('Sleeping',2.8),('Waking',14.9),('Recovered',16.8)]
state={'phase':'start','since':time.monotonic(),'index':0,'rows':[]}
def close(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle']);level.editor_request_end_play()
    (out/'Result.json').write_text(json.dumps({'error':error,'frames':state['rows']},indent=2))
    unreal.log('SLEEP_GAMEPLAY_RENDER '+('FAILED '+error if error else 'OK'))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')
def tick(delta):
    try:
        now=time.monotonic()
        if state['phase']=='start' and now-state['since']>4:
            level.editor_request_begin_play();state['phase']='world'
        elif state['phase']=='world':
            game=editor.get_game_world()
            if not game:return
            player=unreal.GameplayStatics.get_player_character(game,0);npcs=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChasingNPC);cams=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.SceneCapture2D)
            if not player or not npcs or not cams:return
            n=npcs[0]
            if n.get_controller():n.get_controller().destroy_actor()
            n.set_actor_location(unreal.Vector(350,100,100),False,True);n.character_movement.stop_movement_immediately();n.character_movement.disable_movement()
            player.set_actor_location(unreal.Vector(0,0,100),False,True);player.get_controller().set_control_rotation(unreal.Rotator());player.equip_sleep_signal()
            gun=player.railgun;gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
            cap=cams[0].get_component_by_class(unreal.SceneCaptureComponent2D);cap.hide_actor_components(player)
            state.update(game=game,player=player,gun=gun,npc=n,camera=cams[0],capture=cap,phase='warm',since=now)
        elif state['phase'] in ['warm','capture']:
            player=state['player'];cap=state['capture'];cam=state['camera'];game=state['game'];n=state['npc']
            if state['index']<=3:
                pcamera=player.get_component_by_class(unreal.CameraComponent);cam.set_actor_location_and_rotation(pcamera.get_world_location(),pcamera.get_world_rotation(),False,False)
            else:
                state['gun'].set_actor_hidden_in_game(True);cap.set_editor_property('enable_first_person_field_of_view',False);cap.set_editor_property('enable_first_person_scale',False);cap.set_editor_property('fov_angle',65.)
                eye=unreal.Vector(700,-800,480);cam.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(260,50,80)),False,False)
            if state['phase']=='warm':
                if now-state['since']<6:return
                state.update(phase='capture',started=unreal.GameplayStatics.get_time_seconds(game))
            elapsed=unreal.GameplayStatics.get_time_seconds(game)-state['started'];name,deadline=frames[state['index']]
            if elapsed<deadline:return
            unreal.RenderingLibrary.export_render_target(game,cap.texture_target,str(out),name+'.png')
            sleep=n.get_component_by_class(unreal.NPCSleepComponent)
            row={'name':name,'time':elapsed,'state':str(sleep.state),'health':n.get_current_health(),'wrist':str(state['gun'].arms.get_socket_location('hand_r'))}
            state['rows'].append(row)
            if state['index']==0:assert state['gun'].fire_sleep_signal()
            state['index']+=1
            if state['index']==len(frames):close()
    except Exception:close(traceback.format_exc())
unreal.EditorPythonScripting.set_keep_python_script_alive(True);state['handle']=unreal.register_slate_post_tick_callback(tick)
