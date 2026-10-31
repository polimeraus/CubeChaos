"""Import the Fab rifle, restore its purple edge shader and bake its support-hand animation."""
from pathlib import Path
import json, math, runpy, unreal

root=Path(unreal.Paths.project_dir()).resolve()
source=root/'ContentSource/PurpleRailgun'
base='/Game/Weapons/PurpleRailgun'
out=root/'Saved/PurpleRailgun';out.mkdir(parents=True,exist_ok=True)
assets=unreal.AssetToolsHelpers.get_asset_tools();edit=unreal.MaterialEditingLibrary
meta=json.loads((source/'GripSpace.json').read_text())

def import_file(filename,destination,name,options=None):
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(filename),destination_path=destination,destination_name=name,
                    automated=True,replace_existing=True,replace_existing_settings=True,save=True).items():task.set_editor_property(k,v)
    if options:task.set_editor_property('options',options)
    assets.import_asset_tasks([task])
    result=unreal.load_asset(destination+'/'+name)
    assert result,(filename,list(task.imported_object_paths))
    return result

textures={}
for key,filename in [('BaseColor','brushed05_diffuse.jpg'),('Normal','brushed02_normal_opengl.jpg'),
                      ('MetalRough','brushed02_metallic-brushed02_glossiness.png')]:
    t=import_file(source/'Textures'/filename,base+'/Textures','T_PurpleRailgun_'+key)
    t.set_editor_property('srgb',key=='BaseColor')
    if key=='Normal':
        t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property('flip_green_channel',True)
    elif key=='MetalRough':t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_loaded_asset(t,False);textures[key]=t

runpy.run_path(str(Path(__file__).with_name('SetupPurpleRailgunMaterials.py')),run_name='__main__')
body=unreal.load_asset(base+'/Materials/M_PurpleRailgunBody')
energy=unreal.load_asset(base+'/Materials/M_PurpleRailgunEnergy')

options=unreal.FbxImportUI()
for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,
    mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH,automated_import_should_detect_type=False).items():options.set_editor_property(k,v)
for k,v in dict(combine_meshes=True,auto_generate_collision=False,convert_scene=True,convert_scene_unit=True,
    normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():options.get_editor_property('static_mesh_import_data').set_editor_property(k,v)
mesh=import_file(source/'SM_PurpleRailgun.fbx',base+'/Meshes','SM_PurpleRailgun',options)
assert mesh.get_bounds().origin.y>15, 'FBX must import with the barrel pointing along +Y'
slots=mesh.get_editor_property('static_materials')
for i,slot in enumerate(slots):
    name=str(slot.get_editor_property('material_slot_name'));assert name in ['PurpleBody','PurpleEnergy'],name
    slot.set_editor_property('material_interface',energy if name=='PurpleEnergy' else body);slots[i]=slot
mesh.set_editor_property('static_materials',slots)
for name,pos in [('Muzzle',meta['muzzle']),('SupportHand',meta['support_wrist']),('Grip',[0,0,0])]:
    socket=mesh.find_socket(name)
    if not socket:
        socket=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);socket.set_editor_property('socket_name',name);mesh.add_socket(socket)
    socket.set_editor_property('relative_location',unreal.Vector(*pos))
reduction=unreal.StaticMeshReductionOptions()
reduction.set_editor_property('auto_compute_lod_screen_size',False)
reduction.set_editor_property('reduction_settings',[
    unreal.StaticMeshReductionSettings(percent_triangles=1.,screen_size=1.),
    unreal.StaticMeshReductionSettings(percent_triangles=.25,screen_size=.5),
    unreal.StaticMeshReductionSettings(percent_triangles=.08,screen_size=.2)])
assert unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_lods(mesh,reduction)==3
unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)

# Keep the original right-hand/finger pose and bone lengths; solve only the left arm.
def tup(v):return (v.x,v.y,v.z)
def qt(q):return (q.x,q.y,q.z,q.w)
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,s):return tuple(x*s for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def length(a):return math.sqrt(dot(a,a))
def unit(a):return mul(a,1/max(length(a),1e-10))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def inv(q):return (-q[0],-q[1],-q[2],q[3])
def qm(a,b):return (*add(add(mul(b[:3],a[3]),mul(a[:3],b[3])),cross(a[:3],b[:3])),a[3]*b[3]-dot(a[:3],b[:3]))
def arc(a,b):
    a,b=unit(a),unit(b);q=(*cross(a,b),1+dot(a,b));return mul(q,1/length(q))

original=unreal.load_asset('/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS')
path=base+'/Animations/A_PurpleRailgunHold'
animation=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(original.get_path_name(),path)
opts=unreal.AnimPoseEvaluationOptions();opts.set_editor_property('optional_skeletal_mesh',unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
raw=unreal.AnimPoseEvaluationOptions();raw.set_editor_property('optional_skeletal_mesh',opts.get_editor_property('optional_skeletal_mesh'));raw.set_editor_property('should_retarget',False)
ext=unreal.AnimPoseExtensions;world=unreal.AnimPoseSpaces.WORLD;local=unreal.AnimPoseSpaces.LOCAL
frames=unreal.AnimationLibrary.get_num_frames(original);duration=unreal.AnimationLibrary.get_sequence_length(original)
tracks={n:[[],[],[]] for n in ['upperarm_l','lowerarm_l','hand_l']}
for frame in range(frames+1):
    pose=ext.get_anim_pose_at_time(original,duration*frame/frames,opts)
    raw_pose=ext.get_anim_pose_at_time(original,duration*frame/frames,raw)
    bones={n:ext.get_bone_pose(pose,n,world) for n in ['clavicle_l','upperarm_l','lowerarm_l','hand_l']}
    grip=ext.get_socket_pose(pose,'HandGrip_R',world)
    goal=tup(unreal.MathLibrary.transform_location(grip,unreal.Vector(*meta['support_wrist'])))
    shoulder,elbow,wrist=[tup(bones[n].translation) for n in ['upperarm_l','lowerarm_l','hand_l']]
    l1,l2=length(sub(elbow,shoulder)),length(sub(wrist,elbow));d=length(sub(goal,shoulder));direction=unit(sub(goal,shoulder))
    assert abs(l1-l2)<d<l1+l2,(frame,d,l1+l2)
    guide=tup(unreal.MathLibrary.transform_location(grip,unreal.Vector(26,-12,-24)))
    off=sub(guide,shoulder);pole=unit(sub(off,mul(direction,dot(off,direction))))
    along=(l1*l1-l2*l2+d*d)/(2*d);height=math.sqrt(max(0,l1*l1-along*along))
    new_elbow=add(add(shoulder,mul(direction,along)),mul(pole,height))
    upper=qm(arc(sub(elbow,shoulder),sub(new_elbow,shoulder)),qt(bones['upperarm_l'].rotation))
    lower=qm(arc(sub(wrist,elbow),sub(goal,new_elbow)),qt(bones['lowerarm_l'].rotation))
    rotations={'upperarm_l':qm(inv(qt(bones['clavicle_l'].rotation)),upper),'lowerarm_l':qm(inv(upper),lower),
               'hand_l':qm(inv(lower),qt(bones['hand_l'].rotation))}
    for n,values in tracks.items():
        p=ext.get_bone_pose(raw_pose,n,local)
        values[0].append(p.translation);values[1].append(unreal.Quat(*rotations[n]));values[2].append(p.scale3d)
controller=animation.controller;controller.open_bracket('Fit Fab energy rifle support grip',False)
try:
    for n,keys in tracks.items():assert controller.set_bone_track_keys(n,*keys,False)
finally:controller.close_bracket(False)
report=[]
for frame in [0,frames//2,frames]:
    pose=ext.get_anim_pose_at_time(animation,duration*frame/frames,opts);grip=ext.get_socket_pose(pose,'HandGrip_R',world)
    wrist=ext.get_bone_pose(pose,'hand_l',world)
    actual=tup(unreal.MathLibrary.inverse_transform_location(grip,wrist.translation))
    assert length(sub(actual,meta['support_wrist']))<.15,actual
    report.append({'frame':frame,'support_wrist':actual})
assert unreal.EditorAssetLibrary.save_loaded_asset(animation,False)
(out/'Assets.json').write_text(json.dumps({'bounds':str(mesh.get_bounds()),'pose':report,'materials':[m.get_editor_property('material_interface').get_path_name() for m in mesh.get_editor_property('static_materials')]},indent=2))
unreal.log('PURPLE_RAILGUN_ASSETS_SAVED')
if '-SetupPurplePickup' in unreal.SystemLibrary.get_command_line():
    import runpy
    runpy.run_path(str(Path(__file__).with_name('SetupPurpleRailgunPickup.py')),run_name='__main__')
