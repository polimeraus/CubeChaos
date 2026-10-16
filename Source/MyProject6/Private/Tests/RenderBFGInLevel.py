"""Read-only render of the saved fusion cannon in the existing game level."""
from pathlib import Path
import json,time,unreal
out=Path(unreal.Paths.project_saved_dir())/'BFGRework';out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
start=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.PlayerStart))
# Stage a view near the BFG pickup; saved NPCs at PlayerStart obscure the camera.
# This process never saves the map or its temporary visibility changes.
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.Pawn):a.set_is_temporarily_hidden_in_editor(True)
pos=unreal.Vector(-2470,-800,60)
rot=unreal.Rotator(pitch=0,yaw=90,roll=0)
cls=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player=actors.spawn_actor_from_class(cls,start.get_actor_location(),rot)
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(pos,rot,False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector())
gun.equip(player);gun.activate_bfg()
gun.arms.set_editor_property('only_owner_see',False)
gun.weapon_mesh.set_editor_property('only_owner_see',False)
gun.arms.set_update_animation_in_editor(True)
for m in player.get_components_by_class(unreal.SkeletalMeshComponent):m.set_visibility(False)
view=actors.spawn_actor_from_class(unreal.SceneCapture2D,pos,rot)
capture=view.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,val in {'capture_every_frame':True,'always_persist_rendering_state':True,
    'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':90,
    'enable_first_person_field_of_view':True,'first_person_field_of_view':70,
    'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(key,val)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
state={'time':time.monotonic()}
def tick(delta):
    if time.monotonic()-state['time']<20:return
    try:
        unreal.RenderingLibrary.export_render_target(world,target,str(out),'InLevel.png')
        (out/'InLevel.json').write_text(json.dumps({'camera':str(pos),'rotation':str(rot),'mesh':gun.weapon_mesh.static_mesh.get_path_name(),
            'gun_transform':str(gun.weapon_mesh.get_world_transform()),'camera_transform':str(camera.get_world_transform())},indent=2))
        unreal.log('BFG_IN_LEVEL_RENDER_OK')
    finally:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
