"""Capture the real gameplay rig, live procedural NPC pose, electric cage and release."""
import json,time,traceback,runpy
from pathlib import Path
import unreal
runpy.run_path(str(Path(__file__).with_name('VerifyElectricTether.py')))
out=Path(unreal.Paths.project_saved_dir()).resolve()/'ElectricTether/Preview';out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
def box(loc,scale,color):
 a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*loc));a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));a.set_actor_scale3d(unreal.Vector(*scale))
 m=a.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'));m.set_vector_parameter_value('Base Color',unreal.LinearColor(*color,1));return a
box((0,0,-10),(60,60,.2),(.065,.085,.11));box((650,1100,300),(16,.2,6),(.17,.2,.26))
npc=actors.spawn_actor_from_class(unreal.ChasingNPC,unreal.Vector(650,0,100));npc.set_editor_property('spawn_replacements_on_death',False);npc.set_editor_property('contact_energy_drain_per_second',0.);npc.set_editor_property('auto_possess_ai',unreal.AutoPossessAI.DISABLED)
pickup=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/GameplayActors/Pickups/ElectricTether/BP_ArcCagePickup.BP_ArcCagePickup_C'),unreal.Vector(250,-190,0));pickup.set_editor_property('auto_collect',False)
for rot,intensity in [(unreal.Rotator(pitch=-45,yaw=-30),8.),(unreal.Rotator(pitch=-25,yaw=150),3.)]:
 light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,600),rot);light.light_component.set_editor_property('intensity',intensity)
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500));sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.light_component.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.light_component.set_editor_property('intensity',1.5)
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(0,0,170));capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('texture_target',unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8))
for k,v in {'capture_every_frame':False,'capture_on_movement':False,'always_persist_rendering_state':True,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':85.,'enable_first_person_field_of_view':True,'first_person_field_of_view':70.,'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(k,v)
post=capture.get_editor_property('post_process_settings')
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,'override_auto_exposure_bias':True,'auto_exposure_bias':9.5}.items():post.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',post)
state={'phase':'start','since':time.monotonic(),'index':0,'rows':[]}
def close(error=None):
 unreal.unregister_slate_post_tick_callback(state['handle']);level.editor_request_end_play()
 (out/'Result.json').write_text(json.dumps({'error':error,'frames':state['rows']},indent=2));unreal.log('ARC_GAMEPLAY_RENDER '+('FAILED '+error if error else 'OK'))
 unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')
def shot(name):
 n=state['npc'];t=n.get_component_by_class(unreal.NPCElectricTetherComponent)
 unreal.RenderingLibrary.export_render_target(state['game'],state['capture'].texture_target,str(out),name+'.png')
 state['rows'].append({'frame':name,'state':str(t.state),'health':n.get_current_health(),'position':str(n.get_actor_location()),'velocity':str(t.motion_velocity),'captive':state['weapon'].has_target(),'left_grip':str(unreal.MathLibrary.inverse_transform_location(state['gun'].weapon_mesh.get_world_transform(),state['gun'].arms.get_socket_location('hand_l'))),'right_grip':str(unreal.MathLibrary.inverse_transform_location(state['gun'].weapon_mesh.get_world_transform(),state['gun'].arms.get_socket_location('hand_r')))})
def tick(delta):
 try:
  now=time.monotonic()
  if state['phase']=='start' and now-state['since']>5:level.editor_request_begin_play();state['phase']='world'
  elif state['phase']=='world':
   game=editor.get_game_world()
   if not game:return
   player=unreal.GameplayStatics.get_player_character(game,0);npcs=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.ChasingNPC);cams=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.SceneCapture2D)
   if not player or not npcs or not cams:return
   n=npcs[0]
   if n.get_controller():n.get_controller().destroy_actor()
   player.set_actor_location(unreal.Vector(0,0,100),False,True);player.character_movement.disable_movement();player.get_controller().set_control_rotation(unreal.Rotator());player.equip_electric_tether()
   gun=player.railgun;gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
   cap=cams[0].get_component_by_class(unreal.SceneCaptureComponent2D);cap.hide_actor_components(player)
   state.update(game=game,player=player,gun=gun,weapon=player.electric_weapon,npc=n,camera=cams[0],capture=cap,phase='warm',since=now)
  elif state['phase'] in ['warm','bound','swing','release']:
   player=state['player'];cap=state['capture'];cam=state['camera'];n=state['npc'];w=state['weapon'];pc=player.get_controller();pcamera=player.get_component_by_class(unreal.CameraComponent)
   if state['phase'] in ['warm','bound','swing']:cam.set_actor_location_and_rotation(pcamera.get_world_location(),pcamera.get_world_rotation(),False,False)
   else:
    cap.set_editor_property('enable_first_person_field_of_view',False);cap.set_editor_property('enable_first_person_scale',False);cap.set_editor_property('fov_angle',65.)
    eye=unreal.Vector(1400,-1600,850);cam.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,n.get_actor_location()),False,False)
   cap.capture_scene()
   if state['phase']=='warm' and now-state['since']>6:
    eye=pcamera.get_world_location();n.set_actor_location(unreal.Vector(eye.x+650,eye.y,eye.z),False,True);n.character_movement.disable_movement();shot('01_Held');w.start_beam();state.update(phase='bound',since=now)
   elif state['phase']=='bound' and now-state['since']>1:
    assert w.has_target(),'Beam failed to acquire NPC';shot('02_Captured');state.update(phase='swing',since=now)
   elif state['phase']=='swing':
    age=now-state['since'];pc.set_control_rotation(unreal.Rotator(yaw=min(38,age*100),pitch=8*min(1,age/.38)))
    if age>.38:shot('03_Swing');w.stop_beam();state['gun'].set_actor_hidden_in_game(True);state.update(phase='release',since=now,index=0)
   elif state['phase']=='release':
    age=now-state['since'];deadlines=[(.2,'04_Released'),(.75,'05_Impact'),(3.,'06_Recovered')]
    if age>deadlines[state['index']][0]:
     shot(deadlines[state['index']][1]);state['index']+=1
     if state['index']==len(deadlines):close()
 except Exception:close(traceback.format_exc())
unreal.EditorPythonScripting.set_keep_python_script_alive(True);state['handle']=unreal.register_slate_post_tick_callback(tick)
