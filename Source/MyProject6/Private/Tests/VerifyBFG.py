"""Fresh-process verification of saved BFG pickup, assets and first-person rendering."""
from pathlib import Path
import json,time,unreal
out=Path(unreal.Paths.project_saved_dir())/'BFG';out.mkdir(parents=True,exist_ok=True)
base='/Game/Weapons/Railgun/'
for name in ['SM_BFG10K','M_BFGPlasma','S_BFGFire','S_BFGImpact']:
    assert unreal.load_asset(base+name),name
gun_cdo=unreal.get_default_object(unreal.RailgunWeapon)
assert gun_cdo.damage==200
assert abs(gun_cdo.bfg_fire_interval-.2)<.001
assert gun_cdo.bfg_damage==100 and gun_cdo.bfg_splash_damage==100 and gun_cdo.bfg_splash_radius==300
pickup_class=unreal.load_class(None,'/Game/Gameplay/Pickups/BP_BFGPickup.BP_BFGPickup_C')
assert unreal.get_default_object(pickup_class).weapon_reward==unreal.PickupWeapon.BFG
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if '-BFGPreviewOnly' not in unreal.SystemLibrary.get_command_line():
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    pickups=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricPickup)]
    bfg=[a for a in pickups if a.weapon_reward==unreal.PickupWeapon.BFG]
    assert len(bfg)==1 and bfg[0].get_actor_label()=='BP_ParametricPickup4'
    assert len([a for a in pickups if a.weapon_reward==unreal.PickupWeapon.RAILGUN])==3
    assert bfg[0].icon==unreal.PickupMotif.RADIATION_BOMB
    assert bfg[0].icon_color.g>.99
    position=bfg[0].get_actor_location()
    assert all(abs(actual-expected)<.01 for actual,expected in zip((position.x,position.y,position.z),(-2470,-310,-140)))
    (out/'Persistence.json').write_text(json.dumps({'verified':True,'railgun_damage':gun_cdo.damage,
        'bfg_pickup':bfg[0].get_actor_label(),'bfg_interval':gun_cdo.bfg_fire_interval,
        'bfg_direct_damage':gun_cdo.bfg_damage,'bfg_splash_damage':gun_cdo.bfg_splash_damage,
        'bfg_radius_cm':gun_cdo.bfg_splash_radius,'railgun_pickups':3},indent=2))
    unreal.log('BFG_PERSISTENCE_OK')

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
cls=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player=actors.spawn_actor_from_class(cls,unreal.Vector(0,0,100))
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0,0,170),unreal.Rotator(),False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector())
gun.equip(player);gun.activate_bfg()
assert gun.weapon_mesh.get_editor_property('static_mesh').get_name()=='SM_BFG10K'
gun.arms.set_editor_property('only_owner_see',False)
gun.weapon_mesh.set_editor_property('only_owner_see',False)
gun.arms.set_update_animation_in_editor(True)
for m in player.get_components_by_class(unreal.SkeletalMeshComponent):m.set_visibility(False)
for rot,intensity in [(unreal.Rotator(-45,-30,0),56),(unreal.Rotator(-25,150,0),24)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),rot)
    light.light_component.set_editor_property('intensity',intensity)
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
for pos,scale in [(unreal.Vector(1000,0,-5),unreal.Vector(35,30,.1)),(unreal.Vector(1700,0,250),unreal.Vector(.3,20,5))]:
    block=actors.spawn_actor_from_class(unreal.StaticMeshActor,pos);block.static_mesh_component.set_static_mesh(cube);block.set_actor_scale3d(scale)
    material=block.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    material.set_vector_parameter_value('Base Color',unreal.LinearColor(.045,.05,.06,1))
orb=actors.spawn_actor_from_class(unreal.BFGProjectile,unreal.Vector(280,0,170))
capture_actor=actors.spawn_actor_from_class(unreal.SceneCapture2D,camera.get_world_location(),camera.get_world_rotation())
capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,val in {'capture_every_frame':False,'always_persist_rendering_state':True,
    'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':90,
    'enable_first_person_field_of_view':True,'first_person_field_of_view':70,
    'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(key,val)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
post=capture.get_editor_property('post_process_settings')
for key,val in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                'override_auto_exposure_bias':True,'auto_exposure_bias':10}.items():post.set_editor_property(key,val)
capture.set_editor_property('post_process_settings',post)
state={'time':time.monotonic(),'stage':0}
def close():
    unreal.unregister_slate_post_tick_callback(state['handle']);unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    if time.monotonic()-state['time']<(12 if state['stage']==0 else 2):return
    try:
        stage=state['stage']
        if stage==1 and state.pop('warmup',False):
            capture.capture_scene();state['time']=time.monotonic();return
        if stage==0:
            orb.preview_effect(.02,False,capture_actor.get_actor_location());state['warmup']=True
        elif stage==1:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'FirstPerson.png')
            orb.set_actor_hidden_in_game(True)
            pos=unreal.Vector(88,-115,188)
            capture_actor.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,unreal.Vector(75,10,146)),False,False)
            capture.set_editor_property('enable_first_person_field_of_view',False)
            capture.set_editor_property('enable_first_person_scale',False);capture.set_editor_property('fov_angle',55)
        elif stage==2:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'Weapon.png')
            gun.set_actor_hidden_in_game(True);orb.set_actor_hidden_in_game(False)
            orb.set_actor_location(unreal.Vector(600,0,180),False,False)
            pos=unreal.Vector(270,-220,250)
            capture_actor.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,unreal.Vector(600,0,180)),False,False)
            capture.set_editor_property('fov_angle',65)
            orb.preview_effect(.04,False,pos)
        elif stage==3:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'Projectile.png')
            orb.preview_effect(.1,True,capture_actor.get_actor_location())
        else:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'Impact.png')
            unreal.log('BFG_RENDER_OK');close();return
        capture.capture_scene();state['stage']+=1;state['time']=time.monotonic()
    except Exception:close();raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
