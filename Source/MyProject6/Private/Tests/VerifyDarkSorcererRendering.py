import unreal, json, traceback
from pathlib import Path
out=Path(unreal.Paths.project_saved_dir())/'DarkSorcerer'
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state={'frames':0,'task':None,'handle':None}
try:
    world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    npc=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/NPC/DarkSorcerer/BP_DarkSorcerer_NPC.BP_DarkSorcerer_NPC_C'),unreal.Vector(0,0,96),unreal.Rotator())
    npc.set_actor_label('DarkSorcerer_Preview')
    npc.set_editor_property('spawn_replacements_on_death',False)
    comp=npc.mesh
    comp.set_update_animation_in_editor(True)
    comp.set_update_cloth_in_editor(True)
    comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    comp.set_editor_property('enable_update_rate_optimizations',False)
    camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(450,-220,150),unreal.Rotator())
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(0,0,110)),False)
    camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',29)
    camera.get_component_by_class(unreal.CameraComponent).set_editor_property('aspect_ratio',.75)
    for rot,power in [(unreal.Rotator(-35,150,0),12),(unreal.Rotator(-20,-30,0),8),(unreal.Rotator(-60,50,0),6)]:
        light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,250),rot)
        light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(power)
    light=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(400,-240,250),unreal.Rotator())
    light.get_component_by_class(unreal.PointLightComponent).set_intensity(500)
    light.get_component_by_class(unreal.PointLightComponent).set_editor_property('attenuation_radius',2000)
    floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-25),unreal.Rotator())
    floor.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(20,20,.5))
    for cmd in ['r.DefaultFeature.AutoExposure 0','r.EyeAdaptationQuality 0','r.TonemapperGamma 2.2','t.MaxFPS 60']:
        unreal.SystemLibrary.execute_console_command(world,cmd)
    poses=[('Idle','/Game/NPC/DarkSorcerer/Animations/A_DS_MM_Idle',1),('Walk','/Game/NPC/DarkSorcerer/Animations/A_DS_MF_Unarmed_Walk_Fwd',1),('Attack','/Game/NPC/DarkSorcerer/Animations/A_DS_Cast',.8)]
    poses.append(('Death',None,1))
    def stop():
        (out/'Preview.json').write_text(json.dumps(state,default=str,indent=2))
        unreal.EditorLevelLibrary.editor_end_play()
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')
    def tick(dt):
        global npc,comp
        try:
            frame=state['frames'];state['frames']+=1
            if frame==2:unreal.EditorLevelLibrary.editor_play_simulate()
            if frame<60:return
            if frame==60:
                gameworld=unreal.EditorLevelLibrary.get_game_world()
                assert gameworld
                npc=unreal.GameplayStatics.get_all_actors_of_class(gameworld,unreal.DarkSorcererNPC)[0]
                npc.character_movement.disable_movement()
                comp=npc.mesh
                comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
                comp.set_editor_property('enable_update_rate_optimizations',False)
            n=frame-60
            phase=n//180
            if phase>=len(poses):
                stop();return
            if n%180==0:
                name,path,rate=poses[phase]
                if path:
                    comp.play_animation(unreal.load_asset(path),True);comp.set_play_rate(rate)
                    comp.force_cloth_next_update_teleport_and_reset()
                else:
                    unreal.GameplayStatics.apply_damage(npc,1000,None,None,unreal.DamageType)
            if n%180==130:
                state['task']=unreal.AutomationLibrary.take_high_res_screenshot(768,1024,str(out/(poses[phase][0]+'.png')),camera,False,False)
        except Exception:
            state['error']=traceback.format_exc();stop()
    state['handle']=unreal.register_slate_post_tick_callback(tick)
except Exception:
    (out/'Preview.json').write_text(json.dumps({'error':traceback.format_exc()}))
    unreal.SystemLibrary.execute_console_command(None,'QUIT_EDITOR')
