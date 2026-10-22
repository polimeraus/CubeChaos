"""Import SOMNA, author its underhand animation, save reusable defaults and place its pickup."""
from pathlib import Path
import json, math, shutil, runpy, unreal

root=Path(unreal.Paths.project_dir()).resolve();out=root/'Saved/SleepSignal';out.mkdir(parents=True,exist_ok=True)
base='/Game/Weapons/SleepSignal';assets=unreal.AssetToolsHelpers.get_asset_tools();edit=unreal.MaterialEditingLibrary
def create(name,folder,cls,factory):
    return unreal.load_asset(folder+'/'+name) if unreal.EditorAssetLibrary.does_asset_exist(folder+'/'+name) else assets.create_asset(name,folder,cls,factory)
def save(asset):assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False),asset
materials={}
for name,(color,metal,rough,emission) in json.loads((root/'ContentSource/SleepSignal/Palette.json').read_text()).items():
    mi=create('MI_'+name,base+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,unreal.load_asset('/Game/Weapons/Railgun/Materials/M_RailMetal'))
    edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
    for key,value in [('Metallic',metal),('Roughness',rough),('Emission',emission)]:edit.set_material_instance_scalar_parameter_value(mi,key,value)
    save(mi);materials[name]=mi
task=unreal.AssetImportTask()
for key,value in {'filename':str(root/'ContentSource/SleepSignal/SM_SleepSignalDevice.fbx'),'destination_path':base+'/Meshes','destination_name':'SM_SleepSignalDevice','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,value)
options=unreal.FbxImportUI()
for key,value in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(key,value)
for key,value in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():options.static_mesh_import_data.set_editor_property(key,value)
task.set_editor_property('options',options);assets.import_asset_tasks([task])
mesh=unreal.load_asset(base+'/Meshes/SM_SleepSignalDevice');assert mesh
slots=mesh.get_editor_property('static_materials')
for i,slot in enumerate(slots):slot.set_editor_property('material_interface',materials[str(slot.material_slot_name)]);slots[i]=slot
mesh.set_editor_property('static_materials',slots);save(mesh)
task=unreal.AssetImportTask()
for key,value in {'filename':str(root/'ContentSource/SleepSignal/S_SleepSignalHum.wav'),'destination_path':base+'/Audio','destination_name':'S_SleepSignalHum','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,value)
assets.import_asset_tasks([task]);sound=unreal.load_asset(base+'/Audio/S_SleepSignalHum');sound.set_editor_property('looping',True);save(sound)

fx=create('M_SleepSignal',base+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
fx.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE);fx.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);fx.set_editor_property('two_sided',True)
existing=list(edit.get_material_expressions(fx))
def node(cls):
    for i,x in enumerate(existing):
        if isinstance(x,cls):return existing.pop(i)
    return edit.create_material_expression(fx,cls)
shader=node(unreal.MaterialExpressionCustom);shader.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
alpha=node(unreal.MaterialExpressionScalarParameter);alpha.set_editor_property('parameter_name','PulseAlpha');alpha.set_editor_property('default_value',.25)
clock=node(unreal.MaterialExpressionScalarParameter);clock.set_editor_property('parameter_name','Clock')
tint=node(unreal.MaterialExpressionVectorParameter);tint.set_editor_property('parameter_name','Tint');tint.set_editor_property('default_value',unreal.LinearColor(.08,.85,1,1))
sources={'UV':node(unreal.MaterialExpressionTextureCoordinate),'Facing':node(unreal.MaterialExpressionFresnel),'Alpha':alpha,'Clock':clock,'Tint':tint}
pins=[]
for name in sources:
    pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
shader.set_editor_property('inputs',pins)
for name,source in sources.items():assert edit.connect_material_expressions(source,'',shader,name)
shader.set_editor_property('code',Path(__file__).with_name('SleepSignalMaterial.ush').read_text())
for channels,prop in [('rgb',unreal.MaterialProperty.MP_EMISSIVE_COLOR),('a',unreal.MaterialProperty.MP_OPACITY)]:
    mask=node(unreal.MaterialExpressionComponentMask)
    for channel in 'rgba':mask.set_editor_property(channel,channel in channels)
    assert edit.connect_material_expressions(shader,'',mask,'')
    assert edit.connect_material_property(mask,'',prop)
edit.recompile_material(fx);save(fx)
runpy.run_path(str(Path(__file__).with_name('UpdatePickupMotifMaterial.py')))

# Skeleton-preserving two-bone IK: a short crouched underhand delivery, release, and recovery.
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),unreal.Vector())
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector());gun.equip(player)
frame=gun.arms.get_relative_transform()
source=unreal.load_asset('/Game/Weapons/FuseBomb/Animations/A_BombHold');assert source
for name in ['A_SleepSignalHold','A_SleepSignalThrow']:
    path=base+'/Animations/'+name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(),path)
hold=unreal.load_asset(base+'/Animations/A_SleepSignalHold');save(hold)
target=unreal.load_asset(base+'/Animations/A_SleepSignalThrow')
opts=unreal.AnimPoseEvaluationOptions();opts.set_editor_property('optional_skeletal_mesh',gun.arms.get_skeletal_mesh_asset());opts.set_editor_property('should_retarget',False)
ext=unreal.AnimPoseExtensions;local=unreal.AnimPoseSpaces.LOCAL;world=unreal.AnimPoseSpaces.WORLD
pose=ext.get_anim_pose_at_time(source,0,opts)
def v(x):return (x.x,x.y,x.z)
def q(x):return (x.x,x.y,x.z,x.w)
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,s):return tuple(x*s for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def length(a):return math.sqrt(dot(a,a))
def unit(a):return mul(a,1/max(1e-10,length(a)))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def inv(a):return (-a[0],-a[1],-a[2],a[3])
def qm(a,b):return (*add(add(mul(b[:3],a[3]),mul(a[:3],b[3])),cross(a[:3],b[:3])),a[3]*b[3]-dot(a[:3],b[:3]))
def rot(a,b):return qm(qm(a,(*b,0)),inv(a))[:3]
def arc(a,b):
    a,b=unit(a),unit(b);value=(*cross(a,b),1+dot(a,b));return mul(value,1/max(1e-10,length(value)))
def point(p):return v(unreal.MathLibrary.inverse_transform_location(frame,unreal.Vector(*p)))
def ease(t):return t*t*(3-2*t)
keys=[(0,(48,13.5,-27)),(.15,(42,15,-34)),(.30,(61,12,-37)),(.46,(63,18,-43)),(.68,(31,23,-53)),(.86,(32,18,-43)),(1.05,(48,13.5,-27))]
def goal_at(t,side):
    for (a,p),(b,r) in zip(keys,keys[1:]):
        if t<=b+1e-6:
            s=ease(max(0,min(1,(t-a)/(b-a))));g=add(mul(p,1-s),mul(r,s));break
    else:g=keys[-1][1]
    if side=='l':g=(g[0]-18*math.sin(min(1,t/.7)*math.pi)**2,-g[1]-7*math.sin(min(1,t/.7)*math.pi)**2,g[2]-3*math.sin(min(1,t/.7)*math.pi))
    return point(g)
bones=[str(n) for n in ext.get_bone_names(pose)];tracks={n:[[],[],[]] for n in bones}
for index in range(31):
    t=index/30*1.05
    values={n:ext.get_bone_pose(pose,n,local) for n in bones}
    for side in ['l','r']:
        names=['clavicle','upperarm','lowerarm','hand'];b={n:ext.get_bone_pose(pose,n+'_'+side,world) for n in names}
        shoulder,elbow,wrist=[v(b[n].translation) for n in ['upperarm','lowerarm','hand']]
        goal=goal_at(t,side);axis=unit(sub(goal,shoulder));a=length(sub(elbow,shoulder));c=length(sub(wrist,elbow))
        d=min(length(sub(goal,shoulder)),(a+c)*.975);goal=add(shoulder,mul(axis,d))
        pole_offset=sub(point((5,-24 if side=='l' else 24,-100)),shoulder);pole=unit(sub(pole_offset,mul(axis,dot(pole_offset,axis))))
        along=(a*a-c*c+d*d)/(2*d);new_elbow=add(add(shoulder,mul(axis,along)),mul(pole,math.sqrt(max(0,a*a-along*along))))
        upper=qm(arc(sub(elbow,shoulder),sub(new_elbow,shoulder)),q(b['upperarm'].rotation))
        lower=qm(arc(sub(wrist,elbow),sub(goal,new_elbow)),q(b['lowerarm'].rotation))
        hand=q(b['hand'].rotation)
        for name,rotation in [('upperarm',qm(inv(q(b['clavicle'].rotation)),upper)),('lowerarm',qm(inv(upper),lower)),('hand',qm(inv(lower),hand))]:values[name+'_'+side].rotation=unreal.Quat(*rotation)
        opening=math.sin(math.pi*max(0,min(1,(t-.23)/.55)))
        for n in bones:
            if n.endswith('_'+side) and n.startswith(('thumb_','index_','middle_','ring_','pinky_')):
                ref=ext.get_ref_bone_pose(pose,n,local)
                values[n].rotation=unreal.MathLibrary.quat_slerp(values[n].rotation,ref.rotation,opening)
    for n,b in values.items():tracks[n][0].append(b.translation);tracks[n][1].append(b.rotation);tracks[n][2].append(b.scale3d)
ctrl=target.controller;ctrl.open_bracket('Author short underhand signal placement',False)
try:
    ctrl.set_frame_rate(unreal.FrameRate(30,1),False);ctrl.set_number_of_frames(unreal.FrameNumber(30),False)
    ctrl.remove_all_bone_tracks(False)
    for n,data in tracks.items():ctrl.add_bone_curve(n,False);assert ctrl.set_bone_track_keys(n,*data,False),n
finally:ctrl.close_bracket(False)
target.set_editor_property('enable_root_motion',False);save(target)

factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.SleepSignalDevice)
bp=create('BP_SleepSignalDevice',base,unreal.Blueprint,factory)
cdo=unreal.get_default_object(bp.generated_class())
# The first import process constructed native defaults before its assets existed.
cdo.device_mesh.set_static_mesh(mesh)
for key,value in {'signal_sphere_diameter':1000.,'signal_duration':12.,'pulse_interval':1.1,'wake_delay':1.5}.items():cdo.set_editor_property(key,value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp);save(bp)
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ParametricPickup)
pickup_path='/Game/GameplayActors/Pickups/SleepSignal/BP_SleepSignalPickup'
pickup_bp=create('BP_SleepSignalPickup',pickup_path.rsplit('/',1)[0],unreal.Blueprint,factory)
settings={'weapon_reward':unreal.PickupWeapon.SLEEP_SIGNAL,'icon':unreal.PickupMotif.SLEEP_SIGNAL,'shape':unreal.PickupShape.ROUNDED_CUBE,'size':60.,'corner_radius':10.,'body_color':unreal.LinearColor(.018,.025,.06,1),'icon_color':unreal.LinearColor(.1,.75,1,1),'icon_scale':.8,'minimum_glow':1.7,'maximum_glow':3.5,'motion':unreal.PickupMotion.SPIN_AND_BOB,'spin_speed':30.,'bob_height':16.,'bob_period':2.6,'auto_collect':True,'player_only':True}
for k,value in settings.items():unreal.get_default_object(pickup_bp.generated_class()).set_editor_property(k,value)
unreal.BlueprintEditorLibrary.compile_blueprint(pickup_bp);save(pickup_bp)
map_path='/Game/FirstPerson/Lvl_FirstPerson'
backup=out/'Before/Lvl_FirstPerson.umap';backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():shutil.copy2(root/'Content/FirstPerson/Lvl_FirstPerson.umap',backup)
assert level.load_level(map_path)
def record(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'folder':str(a.get_folder_path())}
all_actors=actors.get_all_level_actors();pickups=[a for a in all_actors if isinstance(a,unreal.ParametricPickup)]
existing=[record(a) for a in pickups if a.get_actor_label()!='BP_SleepSignalPickup']
npcs=[a for a in all_actors if isinstance(a,(unreal.ChasingNPC,unreal.WhisperFollower))]
npc_records=[record(a) for a in npcs]
for npc in npcs:
    component=npc.get_component_by_class(unreal.NPCSleepComponent);assert component,npc.get_actor_label()
    default=unreal.get_default_object(npc.get_class()).get_component_by_class(unreal.NPCSleepComponent);assert default
    for key in ['drowsy_duration','fall_duration','wake_duration']:component.set_editor_property(key,default.get_editor_property(key))
pickup=next((a for a in pickups if a.get_actor_label()=='BP_SleepSignalPickup'),None)
if not pickup:
    reference=next(a for a in pickups if a.get_actor_label()=='BP_BombPickup')
    location=reference.get_actor_location();location.y-=150
    pickup=actors.spawn_actor_from_class(pickup_bp.generated_class(),location,reference.get_actor_rotation());pickup.set_actor_label('BP_SleepSignalPickup');pickup.set_folder_path(reference.get_folder_path())
for k,value in settings.items():pickup.set_editor_property(k,value)
pickup.refresh_pickup()
assert [record(a) for a in npcs]==npc_records
assert level.save_current_level()
(out/'Placement.json').write_text(json.dumps({'map':map_path,'pickup':record(pickup),'existing_pickups':existing,'npcs':npc_records,'device_blueprint':base+'/BP_SleepSignalDevice','pickup_blueprint':pickup_path},indent=2))
unreal.log('SLEEP_SIGNAL_SETUP_OK')
