"""Fresh saved-asset checks and two-time render of the actual held railgun material."""
from pathlib import Path
import json,time,runpy,unreal
out=Path(unreal.Paths.project_saved_dir())/'RailTowerUpgrade';out.mkdir(parents=True,exist_ok=True)
if '-RailPreviewOnly' not in unreal.SystemLibrary.get_command_line():
    runpy.run_path(str(Path(__file__).with_name('VerifyTowerOpenings.py')))
base='/Game/Weapons/Railgun'
material=unreal.load_asset(base+'/Materials/M_RailEnergyFlow');assert material
expressions=unreal.MaterialEditingLibrary.get_material_expressions(material)
shader=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionCustom))
assert shader.get_editor_property('code')==Path(__file__).parents[1].joinpath('RailgunEnergy.ush').read_text()
assert any(isinstance(e,unreal.MaterialExpressionTime) for e in expressions)
mi=unreal.load_asset(base+'/Materials/MI_RailEnergy');assert mi.get_editor_property('parent')==material
mesh=unreal.load_asset(base+'/Meshes/SM_QuakeRailgun')
assert next(s for s in mesh.get_editor_property('static_materials') if str(s.material_slot_name)=='RailEnergy').material_interface==mi
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
cls=unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player=actors.spawn_actor_from_class(cls,unreal.Vector(0,0,100))
camera=player.get_component_by_class(unreal.CameraComponent)
camera.detach_from_component(unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD,unreal.DetachmentRule.KEEP_WORLD)
camera.set_world_location_and_rotation(unreal.Vector(0,0,170),unreal.Rotator(),False,False)
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector())
gun.set_editor_property('visuals',player.get_editor_property('railgun_visuals'))
gun.equip(player);gun.activate_railgun()
gun.arms.set_editor_property('only_owner_see',False);gun.weapon_mesh.set_editor_property('only_owner_see',False)
gun.arms.set_update_animation_in_editor(True)
for m in player.get_components_by_class(unreal.SkeletalMeshComponent):m.set_visibility(False)
energy=gun.weapon_mesh.get_material(gun.weapon_mesh.get_material_index('RailEnergy'))
assert isinstance(energy,unreal.MaterialInstanceDynamic)
actual=energy.get_vector_parameter_value('Tint');expected=gun.visuals.color
assert all(abs(getattr(actual,c)-getattr(expected,c))<.001 for c in ['r','g','b'])
assert energy.get_scalar_parameter_value('Emission')>=4
for rot,intensity in [(unreal.Rotator(pitch=-45,yaw=-30),56),(unreal.Rotator(pitch=-25,yaw=150),24)]:
    light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),rot)
    light.light_component.set_editor_property('intensity',intensity)
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
for pos,scale in [(unreal.Vector(1000,0,-5),unreal.Vector(35,30,.1)),(unreal.Vector(1700,0,250),unreal.Vector(.3,20,5))]:
    block=actors.spawn_actor_from_class(unreal.StaticMeshActor,pos);block.static_mesh_component.set_static_mesh(cube);block.set_actor_scale3d(scale)
beam=actors.spawn_actor_from_class(unreal.RailgunBeam,unreal.Vector())
beam.set_editor_property('visuals',gun.visuals)
view=actors.spawn_actor_from_class(unreal.SceneCapture2D,camera.get_world_location(),camera.get_world_rotation())
capture=view.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'capture_every_frame':False,'always_persist_rendering_state':True,
    'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':90,
    'enable_first_person_field_of_view':True,'first_person_field_of_view':70,
    'enable_first_person_scale':True,'first_person_scale':.6}.items():capture.set_editor_property(key,value)
target=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target',target)
post=capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
                  'override_auto_exposure_bias':True,'auto_exposure_bias':10}.items():post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
state={'stage':0,'time':time.monotonic()}
def close():
    unreal.unregister_slate_post_tick_callback(state['handle']);unreal.EditorPythonScripting.set_keep_python_script_alive(False)
def tick(delta):
    if time.monotonic()-state['time']<(12 if state['stage']==0 else 2):return
    try:
        stage=state['stage']
        if stage==0:
            beam.initialize_beam(gun.get_visual_muzzle_location(),unreal.Vector(1680,0,170),.65)
            beam.face_camera(view.get_actor_location());beam.set_preview_age(.035)
        elif stage==1:pass
        elif stage==2:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'RailFirstPerson.png')
            beam.set_is_temporarily_hidden_in_editor(True)
            pos=unreal.Vector(28,-58,209)
            view.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,unreal.Vector(75,12,155)),False,False)
            capture.set_editor_property('enable_first_person_field_of_view',False)
            capture.set_editor_property('enable_first_person_scale',False);capture.set_editor_property('fov_angle',55)
        elif stage==3:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'RailFlowA.png')
        else:
            unreal.RenderingLibrary.export_render_target(world,target,str(out),'RailFlowB.png')
            (out/'RailPersistence.json').write_text(json.dumps({'fresh_process_verified':True,'shader_matches_source':True,
                'color':[actual.r,actual.g,actual.b],'emission':energy.get_scalar_parameter_value('Emission')},indent=2))
            unreal.log('RAIL_FLOW_RENDER_OK');close();return
        capture.capture_scene();state['stage']+=1;state['time']=time.monotonic()
    except Exception:close();raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['handle']=unreal.register_slate_post_tick_callback(tick)
