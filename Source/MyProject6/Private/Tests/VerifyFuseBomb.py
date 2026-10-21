"""Fresh-process persistence and actual Unreal renders of the bomb, hands, pickup and blast."""
from pathlib import Path
import json, time, unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'FuseBomb'
data=json.loads((out/'Placement.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level(data['map'])
pickups={a.get_actor_label():a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricPickup)}
def record(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),
            'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],
            'reward':str(a.weapon_reward),'icon':str(a.icon),'folder':str(a.get_folder_path())}
for row in data['existing']:assert record(pickups[row['label']])==row,row['label']
assert record(pickups['BP_BombPickup'])==data['bomb']
assert len(pickups)==len(data['existing'])+1
bp=unreal.load_asset(data['blueprint']);cdo=unreal.get_default_object(bp.generated_class())
assert cdo.weapon_reward==unreal.PickupWeapon.BOMB and cdo.icon==unreal.PickupMotif.BOMB
fresh=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(25000,0,0))
assert fresh.weapon_reward==unreal.PickupWeapon.BOMB and fresh.icon==unreal.PickupMotif.BOMB
actors.destroy_actor(fresh)
defaults=unreal.get_default_object(unreal.RailgunWeapon)
assert defaults.bomb_fuse_seconds==3. and abs(defaults.bomb_throw_interval-.9)<.001
for folder,name in [('Meshes','SM_FuseBomb'),('Animations','A_BombHold'),('Materials','M_BombEffect'),
                    ('Audio','S_BombThrow'),('Audio','S_BombFuse'),('Audio','S_BombExplosion')]:
    assert unreal.load_asset('/Game/Weapons/FuseBomb/'+folder+'/'+name)
assert abs(defaults.bfg_fire_interval-.22)<.001
fx=unreal.load_asset('/Game/Weapons/FuseBomb/Materials/M_BombEffect')
assert fx.blend_mode==unreal.BlendMode.BLEND_ADDITIVE
nodes=unreal.MaterialEditingLibrary.get_material_expressions(fx)
assert next(n for n in nodes if isinstance(n,unreal.MaterialExpressionCustom)).get_editor_property('code')==Path(__file__).parent.parent.joinpath('BombEffect.ush').read_text()
assert all(n.get_editor_property('texture') is None for n in nodes if isinstance(n,unreal.MaterialExpressionTextureObject)), 'Fire atlases still referenced'
bfg=unreal.load_asset('/Game/Weapons/BFG/Materials/M_BFGPlasma')
assert next(n for n in unreal.MaterialEditingLibrary.get_material_expressions(bfg) if isinstance(n,unreal.MaterialExpressionCustom)).get_editor_property('code')==Path(__file__).parent.parent.joinpath('BFGPlasma.ush').read_text()
report={'fresh_process_verified':True,'pickup_count':len(pickups),'damage_multiplier':2,'npc_damage_multiplier':unreal.get_default_object(unreal.BombProjectile).npc_damage_multiplier,
        'impulse_speed':unreal.get_default_object(unreal.BombProjectile).impulse_speed,'player_energy_damage':unreal.get_default_object(unreal.BombProjectile).player_energy_damage,
        'bomb_interval':defaults.bomb_throw_interval,'bfg_interval':defaults.bfg_fire_interval,'fuse_seconds':defaults.bomb_fuse_seconds,'placement':data}
(out/'Persistence.json').write_text(json.dumps(report,indent=2))
unreal.log('FUSE_BOMB_PERSISTENCE_OK')
if '-NullRHI' in unreal.SystemLibrary.get_command_line():
    raise SystemExit

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
cls=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player=actors.spawn_actor_from_class(cls,unreal.Vector(0,0,100))
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0,0,170),unreal.Rotator(),False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector())
gun.equip(player);gun.activate_bomb()
assert gun.weapon_mesh.get_editor_property('static_mesh').get_name()=='SM_FuseBomb'
# Equip/Activate must update the live instance themselves. Reinitializing the
# editor animation here used to conceal a stale rifle pose in actual gameplay.
gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
for mesh in player.get_components_by_class(unreal.SkeletalMeshComponent):mesh.set_visibility(False)
for rotation,intensity in [(unreal.Rotator(pitch=-45,yaw=-30),12.),(unreal.Rotator(pitch=-25,yaw=150),5.)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),rotation)
    light.light_component.set_editor_property('intensity',intensity)
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
for pos,scale in [(unreal.Vector(350,0,-5),unreal.Vector(12,10,.1)),
                  (unreal.Vector(430,0,200),unreal.Vector(.15,10,4)),
                  (unreal.Vector(350,-280,100),unreal.Vector(12,.15,2)),
                  (unreal.Vector(350,280,100),unreal.Vector(12,.15,2))]:
    block=actors.spawn_actor_from_class(unreal.StaticMeshActor,pos)
    block.static_mesh_component.set_static_mesh(cube);block.set_actor_scale3d(scale)
    surface=block.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    surface.set_vector_parameter_value('Base Color',unreal.LinearColor(.22,.24,.27,1))
capture_actor=actors.spawn_actor_from_class(unreal.SceneCapture2D,camera.get_world_location(),camera.get_world_rotation())
capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'always_persist_rendering_state':True,
                  'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':90.,
                  'enable_first_person_field_of_view':True,'first_person_field_of_view':70.,
                  'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(key,value)
capture.hide_actor_components(player)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
post=capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                  'override_auto_exposure_bias':True,'auto_exposure_bias':10.}.items():post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
state={'next':time.monotonic()+12,'index':0,'pending':False}
frames=['Held','Bomb','Explosion','Neon_018','Neon_040','Neon_085','Neon_140','Pickup']

def view(location,look,fov):
    capture_actor.set_actor_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,look),False,False)
    capture.set_editor_property('fov_angle',fov)
    capture.set_editor_property('enable_first_person_field_of_view',False)
    capture.set_editor_property('enable_first_person_scale',False)

def close():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic()<state['next']:return
    try:
        if state['pending']:
            capture.capture_scene()
            if not state.get('warmed'):
                state['warmed']=True;state['next']=time.monotonic()+.5;return
            unreal.RenderingLibrary.export_render_target(world,target,str(out),frames[state['index']]+'.png')
            state['index']+=1;state['warmed']=False
            if state['index']==len(frames):
                unreal.log('FUSE_BOMB_RENDER_OK');close();return
        i=state['index']
        if i==1:
            gun.set_actor_hidden_in_game(True)
            bomb=actors.spawn_actor_from_class(unreal.BombProjectile,unreal.Vector(100,0,150));state['bomb']=bomb
            view(unreal.Vector(60,-75,176),unreal.Vector(100,0,155),50.)
            bomb.preview_effect(.2,False,capture_actor.get_actor_location())
        elif i==2:
            view(unreal.Vector(-650,-500,450),unreal.Vector(100,0,150),75.)
            state['bomb'].preview_effect(.06,True,capture_actor.get_actor_location())
        elif 3<=i<=6:
            state['bomb'].preview_effect([.18,.4,.85,1.4][i-3],True,capture_actor.get_actor_location())
        elif i==7:
            state['bomb'].set_actor_hidden_in_game(True)
            state['bomb'].fuse_light.set_intensity(0.)
            pickup=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(100,0,30))
            pickup.set_editor_property('preview_motion_in_editor',False);pickup.refresh_pickup()
            view(unreal.Vector(-80,-170,120),unreal.Vector(100,0,60),50.)
        capture.capture_scene();state['pending']=True;state['next']=time.monotonic()+.5
    except Exception:
        close();raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
