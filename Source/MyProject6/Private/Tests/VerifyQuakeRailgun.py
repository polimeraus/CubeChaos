"""Fresh-process asset persistence checks and offscreen render of the saved railgun."""
from pathlib import Path
import json, time, unreal

out=Path(unreal.Paths.project_saved_dir())/'Railgun/Quake'
out.mkdir(parents=True,exist_ok=True)
base='/Game/Weapons/Railgun/'
for name in ['SM_QuakeRailgun','M_RailTrail','M_RailSpiral','S_RailFire','A_RailgunHold']:
    assert unreal.load_asset(base+({'SM':'Meshes','M':'Materials','S':'Audio','A':'Animations'}[name.split('_',1)[0]])+'/'+name), name
cls=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
cdo=unreal.get_default_object(cls)
visuals=cdo.get_editor_property('railgun_visuals')
assert visuals.get_editor_property('spiral')
assert abs(visuals.get_editor_property('color').b-1)<.001
assert abs(visuals.get_editor_property('spiral_radius')-7.5)<.001
assert visuals.get_editor_property('smooth_spiral')
assert not cdo.get_editor_property('railgun_equipped')
gun_cdo=unreal.get_default_object(unreal.RailgunWeapon)
assert abs(gun_cdo.get_editor_property('fire_interval')-1.5)<.001
assert abs(gun_cdo.get_editor_property('trail_duration')-.4)<.001

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if '-RailgunPreviewOnly' not in unreal.SystemLibrary.get_command_line():
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    pickups=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricPickup)]
    assert len(pickups)>=4
    report={'pickups':[a.get_actor_label() for a in pickups], 'fire_interval':gun_cdo.fire_interval,
            'trail_duration':gun_cdo.trail_duration,'damage':gun_cdo.damage,'saved_defaults_verified':True}
    (out/'Persistence.json').write_text(json.dumps(report,indent=2))
    unreal.log('QUAKE_RAILGUN_PERSISTENCE_OK')

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player=actors.spawn_actor_from_class(cls,unreal.Vector(0,0,100))
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0,0,170),unreal.Rotator(0,0,0),False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector(0,0,0))
gun.equip(player);gun.activate_railgun()
assert gun.weapon_mesh.get_editor_property('static_mesh').get_name()=='SM_QuakeRailgun'
gun.arms.set_editor_property('only_owner_see',False)
gun.weapon_mesh.set_editor_property('only_owner_see',False)
gun.arms.set_update_animation_in_editor(True)
for m in player.get_components_by_class(unreal.SkeletalMeshComponent):m.set_visibility(False)
for rot,intensity in [(unreal.Rotator(-45,-30,0),7),(unreal.Rotator(-25,150,0),3)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),rot)
    light.light_component.set_editor_property('intensity',intensity*8)
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
for pos,scale in [(unreal.Vector(1000,0,-5),unreal.Vector(35,30,.1)),(unreal.Vector(1700,0,250),unreal.Vector(.3,20,5))]:
    block=actors.spawn_actor_from_class(unreal.StaticMeshActor,pos)
    block.static_mesh_component.set_static_mesh(cube);block.set_actor_scale3d(scale)
    material=block.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Game/LevelPrototyping/Materials/M_FlatCol'))
    material.set_vector_parameter_value('Base Color',unreal.LinearColor(.045,.05,.06,1))
beam=actors.spawn_actor_from_class(unreal.RailgunBeam,unreal.Vector(0,0,0))
beam.set_editor_property('visuals',visuals)
capture_actor=actors.spawn_actor_from_class(unreal.SceneCapture2D,camera.get_world_location(),camera.get_world_rotation())
capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.set_editor_property('capture_every_frame',False)
capture.set_editor_property('always_persist_rendering_state',True)
capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('fov_angle',90)
capture.set_editor_property('enable_first_person_field_of_view',True)
capture.set_editor_property('first_person_field_of_view',70)
capture.set_editor_property('enable_first_person_scale',True)
capture.set_editor_property('first_person_scale',.6)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
post=capture.get_editor_property('post_process_settings')
post.set_editor_property('override_auto_exposure_method',True)
post.set_editor_property('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL)
post.set_editor_property('override_auto_exposure_bias',True)
post.set_editor_property('auto_exposure_bias',10)
capture.set_editor_property('post_process_settings',post)
state={'time':time.monotonic(),'stage':0}

def close():
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    if time.monotonic()-state['time'] < (12 if state['stage']==0 else 2):return
    try:
        stage=state['stage']
        # A newly built DynamicMesh proxy reaches the render thread after the first
        # capture. Render a second frame before exporting the initial shot.
        if stage==1 and state.pop('warmup',False):
            capture.capture_scene();state['time']=time.monotonic();return
        if stage==0:
            beam.initialize_beam(gun.get_visual_muzzle_location(),unreal.Vector(1680,0,170),.4)
            beam.set_impact(unreal.Vector(1680,0,170))
            beam.face_camera(capture_actor.get_actor_location())
            beam.set_preview_age(.035)
            state['warmup']=True
            (out/'RenderTransforms.json').write_text(json.dumps({
                'camera':str(camera.get_world_transform()),'capture':str(capture_actor.get_actor_transform()),
                'muzzle_world':str(gun.muzzle.get_world_location()),'muzzle_visual':str(gun.get_visual_muzzle_location()),
                'beam':str(beam.get_actor_transform()),'core':str(beam.trail.get_world_transform()),
                'spiral':str(beam.spiral.get_world_transform())},indent=2))
        elif stage==1:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'FirstPerson.png')
            gun.set_actor_hidden_in_game(True)
        elif stage==2:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'BeamOnly.png')
            capture_actor.set_actor_location_and_rotation(unreal.Vector(540,-630,300),
                unreal.MathLibrary.find_look_at_rotation(unreal.Vector(540,-630,300),unreal.Vector(800,0,170)),False,False)
            capture.set_editor_property('enable_first_person_field_of_view',False)
            capture.set_editor_property('enable_first_person_scale',False)
            capture.set_editor_property('fov_angle',70)
            beam.face_camera(capture_actor.get_actor_location())
            beam.set_preview_age(.12)
        elif stage==3:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'Spiral.png')
            beam.set_preview_age(.5)
        elif stage==4:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'SpiralFade.png')
            gun.set_actor_hidden_in_game(False)
            beam.set_actor_hidden_in_game(True)
            capture_actor.set_actor_location_and_rotation(unreal.Vector(80,-105,182),
                unreal.MathLibrary.find_look_at_rotation(unreal.Vector(80,-105,182),unreal.Vector(80,14,153)),False,False)
            capture.set_editor_property('fov_angle',55)
        else:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'Weapon.png')
            unreal.log('QUAKE_RAILGUN_RENDER_OK');close();return
        capture.capture_scene()
        state['stage']+=1;state['time']=time.monotonic()
    except Exception:
        close();raise

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
