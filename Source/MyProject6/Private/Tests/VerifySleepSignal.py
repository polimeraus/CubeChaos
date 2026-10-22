"""Fresh-process saved-asset verification, followed by real Unreal scene captures."""
import csv,json,time,traceback
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'SleepSignal';preview=out/'Preview';preview.mkdir(parents=True,exist_ok=True)
data=json.loads((out/'Placement.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level(data['map'])
by_label={a.get_actor_label():a for a in actors.get_all_level_actors()}
def record(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'folder':str(a.get_folder_path())}
for row in data['existing_pickups']+data['npcs']+[data['pickup']]:assert record(by_label[row['label']])==row,row
pick=by_label['BP_SleepSignalPickup'];assert pick.weapon_reward==unreal.PickupWeapon.SLEEP_SIGNAL and pick.icon==unreal.PickupMotif.SLEEP_SIGNAL
defaults=unreal.get_default_object(unreal.RailgunWeapon);device=unreal.load_asset(data['device_blueprint']);d=unreal.get_default_object(device.generated_class())
assert defaults.sleep_device_class==device.generated_class()
assert d.signal_sphere_diameter==1000 and d.signal_duration==12
assert d.device_mesh.static_mesh.get_name()=='SM_SleepSignalDevice'
assert defaults.sleep_throw_speed<defaults.bomb_throw_speed*.4
classes={a.get_class() for a in by_label.values() if isinstance(a,(unreal.ChasingNPC,unreal.WhisperFollower))}
for asset in unreal.EditorAssetLibrary.list_assets('/Game/NPC',True,False):
    if '/BP_' not in asset:continue
    bp=unreal.load_asset(asset)
    if not isinstance(bp,unreal.Blueprint):continue
    cdo=unreal.get_default_object(bp.generated_class())
    if isinstance(cdo,(unreal.ChasingNPC,unreal.WhisperFollower)):classes.add(bp.generated_class())
for cls in classes:
    cdo=unreal.get_default_object(cls);sleep=cdo.get_component_by_class(unreal.NPCSleepComponent);assert sleep,cls
    assert abs(sleep.wake_duration-2.9)<.001,cls
    fresh=actors.spawn_actor_from_class(cls,unreal.Vector(25000,0,0));component=fresh.get_component_by_class(unreal.NPCSleepComponent);assert component
    for key in ['drowsy_duration','fall_duration','wake_duration']:assert component.get_editor_property(key)==sleep.get_editor_property(key)
    actors.destroy_actor(fresh)
for row in data['npcs']:
    actor=by_label[row['label']];component=actor.get_component_by_class(unreal.NPCSleepComponent);cdo=unreal.get_default_object(actor.get_class()).get_component_by_class(unreal.NPCSleepComponent)
    for key in ['drowsy_duration','fall_duration','wake_duration']:assert component.get_editor_property(key)==cdo.get_editor_property(key)
for name in ['A_SleepSignalHold','A_SleepSignalThrow']:assert unreal.load_asset('/Game/Weapons/SleepSignal/Animations/'+name)
fx=unreal.load_asset('/Game/Weapons/SleepSignal/Materials/M_SleepSignal');assert fx.blend_mode==unreal.BlendMode.BLEND_ADDITIVE
(out/'Persistence.json').write_text(json.dumps({'fresh_process_verified':True,'npc_classes':sorted(c.get_path_name() for c in classes),'placed_npcs':len(data['npcs']),'pickup':data['pickup'],'diameter_cm':d.signal_sphere_diameter,'duration_s':d.signal_duration},indent=2))
unreal.log('SLEEP_SIGNAL_PERSISTENCE_OK')
if '-NullRHI' in unreal.SystemLibrary.get_command_line():raise SystemExit

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
sun=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,600),unreal.Rotator(-45,-35,0));sun.light_component.set_editor_property('intensity',12.)
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500));sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.light_component.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.light_component.set_editor_property('intensity',2.)
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-6));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(unreal.Vector(50,50,.1))
camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(500,-1400,600));camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(0,0,85)),False)
capture=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
for key,value in {'projection_type':unreal.CameraProjectionMode.ORTHOGRAPHIC,'ortho_width':1450.,'capture_every_frame':False,'always_persist_rendering_state':True,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR}.items():capture.set_editor_property(key,value)
target=unreal.RenderingLibrary.create_render_target2d(world,1600,700,unreal.TextureRenderTargetFormat.RTF_RGBA8);capture.set_editor_property('texture_target',target)
post=capture.get_editor_property('post_process_settings')
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,'override_auto_exposure_bias':True,'auto_exposure_bias':3.}.items():post.set_editor_property(key,value)
capture.set_editor_property('post_process_settings',post)
spawned=[];cases=sorted((out/'Poses').glob('*_Awake.csv'))
assert cases
def clear():
    for a in spawned:actors.destroy_actor(a)
    spawned.clear()
def show_pose_case(path):
    clear()
    for i,stage in enumerate(['Drowsy','Falling','Sleeping','Waking']):
        lines=path.with_name(path.name.replace('_Awake.csv','_'+stage+'.csv')).read_text(encoding='utf-8-sig').splitlines()
        default=unreal.get_default_object(unreal.load_class(None,lines[0]));source=default.get_component_by_class(unreal.SkeletalMeshComponent)
        offset=unreal.Vector((1.5-i)*330,(1.5-i)*118,0)
        actor=actors.spawn_actor_from_class(unreal.Actor,offset);spawned.append(actor)
        subsystem=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);handles=subsystem.k2_gather_subobject_data_for_instance(actor)
        handle,reason=subsystem.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.PoseableMeshComponent))
        mesh=actor.get_component_by_class(unreal.PoseableMeshComponent);assert mesh,str(reason)
        mesh.set_skinned_asset_and_update(source.skeletal_mesh_asset)
        for slot in range(source.get_num_materials()):mesh.set_material(slot,source.get_material(slot))
        for row in csv.reader(lines[1:]):
            v=[float(x) for x in row[1:]];t=unreal.Transform(location=unreal.Vector(*v[:3]),rotation=unreal.Rotator(pitch=v[3],yaw=v[4],roll=v[5]),scale=unreal.Vector(*v[6:9]))
            mesh.set_bone_transform_by_name(row[0],t,unreal.BoneSpaces.COMPONENT_SPACE)
        unreal.NPCHitBendAnimInstance.refresh_preview_pose(mesh)
        unreal.log('POSE_PREVIEW '+stage+' '+str(mesh.get_bone_transform_by_name('head',unreal.BoneSpaces.COMPONENT_SPACE)))
def show_device():
    clear();capture.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE);capture.set_editor_property('fov_angle',60.)
    camera.set_actor_location_and_rotation(unreal.Vector(280,-400,260),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(280,-400,260),unreal.Vector(0,0,30)),False,False)
    unit=actors.spawn_actor_from_class(device.generated_class(),unreal.Vector(-85,0,12));unit.set_actor_scale3d(unreal.Vector(3,3,3));unit.set_actor_location(unreal.Vector(-85,0,36),False,False);spawned.append(unit)
    pickup=actors.spawn_actor_from_class(unreal.load_asset(data['pickup_blueprint']).generated_class(),unreal.Vector(90,0,0));pickup.set_editor_property('auto_collect',False);pickup.refresh_pickup();spawned.append(pickup)
    unreal.log('PICKUP_MATERIAL '+str(pickup.get_component_by_class(unreal.DynamicMeshComponent).get_material(0)))
    unreal.log('DEVICE_BOUNDS '+str(unit.get_actor_bounds(False))+' visible='+str(unit.device_mesh.is_visible())+' scale='+str(unit.device_mesh.get_world_scale()))
    unreal.log('DEVICE_LOCATION '+str(unit.get_actor_location())+' '+str(unit.device_mesh.get_world_location())+' '+str(unit.device_mesh.static_mesh))
def show_device_close():
    clear();capture.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE);capture.set_editor_property('fov_angle',30.)
    eye=unreal.Vector(100,-120,85)
    camera.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(0,0,5)),False,False)
    unit=actors.spawn_actor_from_class(device.generated_class(),unreal.Vector(0,0,12));spawned.append(unit)
    unreal.log('DEVICE_CLOSE '+str(unit.device_mesh.get_world_scale())+' '+str(unit.get_actor_bounds(False)))
def show_field():
    clear();capture.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE);capture.set_editor_property('fov_angle',65.)
    camera.set_actor_location_and_rotation(unreal.Vector(700,-1100,700),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(700,-1100,700),unreal.Vector(0,0,70)),False,False)
    unit=actors.spawn_actor_from_class(device.generated_class(),unreal.Vector(0,0,12));unit.preview_signal(.6);spawned.append(unit)
    for c in unit.get_components_by_class(unreal.StaticMeshComponent):unreal.log('DEVICE_COMPONENT '+c.get_name()+' '+str(c.get_material(0))+' '+str(c.get_world_location())+' '+str(c.is_visible()))
    # Use the real sampled sleeping pose beside the wavefronts.
    path=next((out/'Poses').glob('ChasingNPC_Sleeping.csv'));lines=path.read_text(encoding='utf-8-sig').splitlines();source=unreal.get_default_object(unreal.ChasingNPC).get_component_by_class(unreal.SkeletalMeshComponent)
    actor=actors.spawn_actor_from_class(unreal.Actor,unreal.Vector(180,0,0));spawned.append(actor)
    sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);handles=sub.k2_gather_subobject_data_for_instance(actor);sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.PoseableMeshComponent));m=actor.get_component_by_class(unreal.PoseableMeshComponent);m.set_skinned_asset_and_update(source.skeletal_mesh_asset)
    for slot in range(source.get_num_materials()):m.set_material(slot,source.get_material(slot))
    for row in csv.reader(lines[1:]):
        v=[float(x) for x in row[1:]];m.set_bone_transform_by_name(row[0],unreal.Transform(location=unreal.Vector(*v[:3]),rotation=unreal.Rotator(pitch=v[3],yaw=v[4],roll=v[5]),scale=unreal.Vector(*v[6:9])),unreal.BoneSpaces.COMPONENT_SPACE)
    unreal.NPCHitBendAnimInstance.refresh_preview_pose(m)
jobs=[(p.stem.replace('_Awake',''),lambda p=p:show_pose_case(p)) for p in cases]+[('DeviceAndPickup',show_device),('DeviceCloseup',show_device_close),('SignalField',show_field)]
state={'index':0,'next':time.monotonic()+12,'phase':'create','captures':0}
def finish():
    unreal.unregister_slate_post_tick_callback(state['handle']);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')
def tick(delta):
    if time.monotonic()<state['next']:return
    try:
        name,make=jobs[state['index']]
        if state['phase']=='create':make();state.update(phase='capture',captures=4,next=time.monotonic()+2.5)
        elif state['phase']=='capture':
            capture.capture_scene();state['captures']-=1;state['next']=time.monotonic()+.25
            if state['captures']==0:state['phase']='save'
        else:
            unreal.RenderingLibrary.export_render_target(world,target,str(preview),name+'.png');state['index']+=1
            if state['index']==len(jobs):unreal.log('SLEEP_SIGNAL_RENDER_OK');finish()
            else:state.update(phase='create',next=time.monotonic()+.1)
    except Exception:
        (out/'RenderError.txt').write_text(traceback.format_exc());finish();raise
unreal.EditorPythonScripting.set_keep_python_script_alive(True);state['handle']=unreal.register_slate_post_tick_callback(tick)
