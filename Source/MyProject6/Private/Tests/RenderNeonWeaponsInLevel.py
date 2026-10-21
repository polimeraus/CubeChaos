"""Render saved grips and neon effects against the actual level; never save the staging actors."""
from pathlib import Path
import time, unreal
out=Path(unreal.Paths.project_saved_dir()).resolve()/'NeonBlast'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/FirstPerson/Lvl_FirstPerson')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
pos=unreal.Vector(-2470,-800,60);rot=unreal.Rotator(yaw=90)
player=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),pos,rot)
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(pos,rot,False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector());gun.equip(player);gun.activate_bomb()
gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
view=actors.spawn_actor_from_class(unreal.SceneCapture2D,pos,rot)
capture=view.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'always_persist_rendering_state':True,
                  'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':90.,
                  'enable_first_person_field_of_view':True,'first_person_field_of_view':70.,
                  'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(key,value)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.Pawn):capture.hide_actor_components(actor)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
frames=['HeldInLevel','Blast008','Blast020','Blast040','Blast080','BFGFlight','BFGImpact']
state={'next':time.monotonic()+20,'index':0}

def close():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic()<state['next']:return
    try:
        i=state['index']
        # The commandlet's editor world does not reliably tick automatic scene
        # captures. Explicitly render after new dynamic-mesh proxies are ready.
        capture.capture_scene()
        if not state.get('warmed'):
            state['warmed']=True;state['next']=time.monotonic()+.5;return
        unreal.RenderingLibrary.export_render_target(world,target,str(out),frames[i]+'.png')
        i+=1;state['index']=i;state['warmed']=False
        if i==len(frames):unreal.log('NEON_WEAPONS_LEVEL_RENDER_OK');close();return
        if i==1:
            gun.set_actor_hidden_in_game(True)
            centre=unreal.Vector(-2470,-50,-100)
            state['bomb']=actors.spawn_actor_from_class(unreal.BombProjectile,centre)
            eye=unreal.Vector(-2470,-1050,160)
            view.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,centre),False,False)
            capture.set_editor_property('enable_first_person_field_of_view',False)
            capture.set_editor_property('enable_first_person_scale',False)
        if 1<=i<=4:state['bomb'].preview_effect([.08,.2,.4,.8][i-1],True,view.get_actor_location())
        elif i==5:
            actors.destroy_actor(state['bomb'])
            state['bfg']=actors.spawn_actor_from_class(unreal.BFGProjectile,unreal.Vector(-2470,-350,20),rot)
            state['bfg'].preview_effect(.3,False,view.get_actor_location())
        elif i==6:state['bfg'].preview_effect(.12,True,view.get_actor_location())
        state['next']=time.monotonic()+1.
    except Exception:close();raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
