"""Import bomb art/audio and author a two-handed cupping pose without stretching bones."""
from pathlib import Path
import json, math, unreal

root=Path(unreal.Paths.project_dir()).resolve()
base='/Game/Weapons/FuseBomb'
out=root/'Saved/FuseBomb';out.mkdir(parents=True,exist_ok=True)
assets=unreal.AssetToolsHelpers.get_asset_tools();edit=unreal.MaterialEditingLibrary

def create(name, folder, cls, factory):
    path=folder+'/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name,folder,cls,factory)

if '-BombPresentationOnly' not in unreal.SystemLibrary.get_command_line():
    surface=unreal.load_asset('/Game/Weapons/Railgun/Materials/M_RailMetal');assert surface
    materials={}
    for name,(color,metal,rough,emission) in json.loads((root/'ContentSource/FuseBomb/Palette.json').read_text()).items():
        mi=create('MI_'+name,base+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi,surface)
        edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
        for key,value in [('Metallic',metal),('Roughness',rough),('Emission',emission)]:edit.set_material_instance_scalar_parameter_value(mi,key,value)
        assert unreal.EditorAssetLibrary.save_loaded_asset(mi,False)
        materials[name]=mi

    task=unreal.AssetImportTask()
    for key,value in {'filename':str(root/'ContentSource/FuseBomb/SM_FuseBomb.fbx'),'destination_path':base+'/Meshes',
                      'destination_name':'SM_FuseBomb','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,value)
    options=unreal.FbxImportUI()
    for key,value in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,
                      'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(key,value)
    data=options.get_editor_property('static_mesh_import_data')
    for key,value in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,
                      'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():data.set_editor_property(key,value)
    task.set_editor_property('options',options);assets.import_asset_tasks([task])
    mesh=unreal.load_asset(base+'/Meshes/SM_FuseBomb');assert mesh
    slots=mesh.get_editor_property('static_materials')
    for i,slot in enumerate(slots):
        slot.set_editor_property('material_interface',materials[str(slot.get_editor_property('material_slot_name'))]);slots[i]=slot
    mesh.set_editor_property('static_materials',slots)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
    for name in ['S_BombThrow','S_BombFuse','S_BombExplosion']:
        task=unreal.AssetImportTask()
        for key,value in {'filename':str(root/'ContentSource/FuseBomb'/(name+'.wav')),'destination_path':base+'/Audio',
                          'destination_name':name,'automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(key,value)
        assets.import_asset_tasks([task]);sound=unreal.load_asset(base+'/Audio/'+name);assert sound
        sound.set_editor_property('looping',name=='S_BombFuse')
        assert unreal.EditorAssetLibrary.save_loaded_asset(sound,False)

else:
    mesh=unreal.load_asset(base+'/Meshes/SM_FuseBomb');assert mesh

# Keep both weapon shaders synchronized with their checked-in source.
import runpy
runpy.run_path(str(Path(__file__).with_name('SetupNeonWeaponEffects.py')))

# Match the existing native rig transform exactly, then solve both arms in its frame.
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),unreal.Vector())
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector());gun.equip(player)
arm_frame=gun.arms.get_relative_transform()

def v(p):return (p.x,p.y,p.z)
def q(p):return (p.x,p.y,p.z,p.w)
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
    a,b=unit(a),unit(b)
    if dot(a,b)<-.99999:
        axis=unit(cross(a,(0,0,1) if abs(a[2])<.9 else (0,1,0)))
        return (*axis,0)
    value=(*cross(a,b),1+dot(a,b));return mul(value,1/length(value))
def point(p):return v(unreal.MathLibrary.inverse_transform_location(arm_frame,unreal.Vector(*p)))
def direction(p):return unit(rot(inv(q(arm_frame.rotation)),p))

source=unreal.load_asset('/Game/Weapons/Railgun/Animations/A_RailgunHold')
path=base+'/Animations/A_BombHold'
target=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(),path)
options=unreal.AnimPoseEvaluationOptions();options.set_editor_property('optional_skeletal_mesh',gun.arms.get_skeletal_mesh_asset())
raw_options=unreal.AnimPoseEvaluationOptions();raw_options.set_editor_property('optional_skeletal_mesh',options.optional_skeletal_mesh);raw_options.set_editor_property('should_retarget',False)
ext=unreal.AnimPoseExtensions;world,local=unreal.AnimPoseSpaces.WORLD,unreal.AnimPoseSpaces.LOCAL
frames=unreal.AnimationLibrary.get_num_frames(source);duration=unreal.AnimationLibrary.get_sequence_length(source)
tracks={bone+'_'+side:[[],[],[]] for side in ['l','r'] for bone in ['upperarm','lowerarm','hand']}
tracks['root']=[[],[],[]]
report=[]
for frame in range(frames+1):
    pose=ext.get_anim_pose_at_time(source,duration*frame/frames,options)
    raw_pose=ext.get_anim_pose_at_time(source,duration*frame/frames,raw_options)
    # The rifle animation turns the torso: one shoulder is 25 cm farther forward.
    # Rigidly square the torso before IK, so both elbows can hang below the frame.
    # This transforms the whole hierarchy and preserves every bone's length.
    shoulders=[v(ext.get_bone_pose(pose,'upperarm_'+s,world).translation) for s in ['l','r']]
    shoulder_axis=unit(sub(shoulders[1],shoulders[0]))
    body_q=arc(shoulder_axis,direction((0,1,0)))
    centre=mul(add(*shoulders),.5)
    body_offset=sub(point((10,0,-10)),rot(body_q,centre))
    def square_body(transform):
        transform.translation=unreal.Vector(*add(rot(body_q,v(transform.translation)),body_offset))
        transform.rotation=unreal.Quat(*qm(body_q,q(transform.rotation)))
        return transform
    root_pose=square_body(ext.get_bone_pose(raw_pose,'root',local))
    tracks['root'][0].append(root_pose.translation)
    tracks['root'][1].append(root_pose.rotation)
    tracks['root'][2].append(root_pose.scale3d)
    for side,sign in [('l',-1),('r',1)]:
        names=['clavicle','upperarm','lowerarm','hand','index_01','middle_01','pinky_01']
        bones={n:square_body(ext.get_bone_pose(pose,n+'_'+side,world)) for n in names}
        shoulder,elbow,wrist=[v(bones[n].translation) for n in ['upperarm','lowerarm','hand']]
        goal=point((48,sign*13.5,-27))
        upper_len,lower_len=length(sub(elbow,shoulder)),length(sub(wrist,elbow))
        d=length(sub(goal,shoulder));axis=unit(sub(goal,shoulder))
        assert abs(upper_len-lower_len)<d<upper_len+lower_len,(side,'Out of reach',d,upper_len+lower_len)
        pole_offset=sub(point((5,sign*20,-100)),shoulder)
        pole=unit(sub(pole_offset,mul(axis,dot(pole_offset,axis))))
        along=(upper_len**2-lower_len**2+d*d)/(2*d)
        new_elbow=add(add(shoulder,mul(axis,along)),mul(pole,math.sqrt(max(0,upper_len**2-along**2))))
        upper_q=qm(arc(sub(elbow,shoulder),sub(new_elbow,shoulder)),q(bones['upperarm'].rotation))
        lower_q=qm(arc(sub(wrist,elbow),sub(goal,new_elbow)),q(bones['lowerarm'].rotation))
        forward=unit(sub(v(bones['middle_01'].translation),wrist))
        across=unit(sub(v(bones['index_01'].translation),v(bones['pinky_01'].translation)))
        desired_forward=direction((1,0,.08))
        # Index-to-pinky is the anatomical thumb/up direction on BOTH hands.
        # Inferring a palm normal from the rifle socket flipped the right wrist.
        across=unit(sub(across,mul(forward,dot(across,forward))))
        desired_across=direction((0,0,1))
        desired_across=unit(sub(desired_across,mul(desired_forward,dot(desired_across,desired_forward))))
        align=arc(forward,desired_forward)
        twist=arc(rot(align,across),desired_across)
        hand_q=qm(qm(twist,align),q(bones['hand'].rotation))
        rotations={'upperarm':qm(inv(q(bones['clavicle'].rotation)),upper_q),
                   'lowerarm':qm(inv(upper_q),lower_q),'hand':qm(inv(lower_q),hand_q)}
        for name,rotation in rotations.items():
            original=ext.get_bone_pose(raw_pose,name+'_'+side,local);values=tracks[name+'_'+side]
            values[0].append(original.translation);values[1].append(unreal.Quat(*rotation));values[2].append(original.scale3d)
        if frame==0:report.append({'side':side,'wrist_target':goal,'thumb_up':desired_across,'source_hand':str(bones['hand'])})
controller=target.controller;controller.open_bracket('Cup the fuse bomb with both hands',False)
try:
    for name,(positions,rotations,scales) in tracks.items():assert controller.set_bone_track_keys(name,positions,rotations,scales,False),name
finally:controller.close_bracket(False)
# Use Manny's reference anatomy, including metacarpals. The previous sphere-fit
# independently aimed phalanges and twisted the thumbs out of their hinge planes.
finger_names=[str(b) for b in ext.get_bone_names(raw_pose)
              if str(b).startswith(('thumb_','index_','middle_','ring_','pinky_')) and str(b).endswith(('_l','_r'))]
controller.open_bracket('Restore mannequin finger anatomy',False)
try:
    for name in finger_names:
        ref=ext.get_ref_bone_pose(raw_pose,name,local)
        assert controller.set_bone_track_keys(name,[ref.translation]*(frames+1),[ref.rotation]*(frames+1),[ref.scale3d]*(frames+1),False)
finally:controller.close_bracket(False)
finger_tracks={f'{finger}_{joint:02d}_{side}':[[],[],[]]
               for side in ['l','r'] for finger in ['thumb','index','middle','ring','pinky'] for joint in [1,2,3]}
for frame in range(frames+1):
    pose=ext.get_anim_pose_at_time(target,duration*frame/frames,options)
    raw_pose=ext.get_anim_pose_at_time(target,duration*frame/frames,raw_options)
    for side in ['l','r']:
        for finger in ['thumb','index','middle','ring','pinky']:
            names=[f'{finger}_{i:02d}_{side}' for i in [1,2,3]]
            bones=[ext.get_bone_pose(pose,name,world) for name in names]
            positions=[v(b.translation) for b in bones]
            parent=gun.arms.get_skeletal_mesh_asset().get_bone_parent(names[0])
            parent_q=q(ext.get_bone_pose(pose,parent,world).rotation)
            for i,name in enumerate(names):
                # Manny's reference fingers already have a relaxed curl. Keep
                # that authored articulation instead of adding another fist.
                new_q=q(bones[i].rotation)
                if finger=='thumb':
                    # Oppose the thumb across the near face of the sphere,
                    # keeping it outside the shell and giving each joint a soft arc.
                    sign=-1 if side=='l' else 1
                    aim=[(-.7,-sign*.7,.10),(-.8,-sign*.4,.30),(-.4,-sign*.8,.40)][i]
                    old_direction=sub(positions[i+1],positions[i]) if i<2 else rot(new_q,v(ext.get_ref_bone_pose(raw_pose,name,local).translation))
                    new_q=qm(arc(old_direction,direction(aim)),new_q)
                original=ext.get_bone_pose(raw_pose,name,local);values=finger_tracks[name]
                values[0].append(original.translation)
                values[1].append(unreal.Quat(*qm(inv(parent_q),new_q)))
                values[2].append(original.scale3d)
                parent_q=new_q
controller.open_bracket('Relaxed anatomical mannequin cup',False)
try:
    for name,values in finger_tracks.items():assert controller.set_bone_track_keys(name,*values,False)
finally:controller.close_bracket(False)
for frame in [0,frames//2,frames]:
    corrected=ext.get_anim_pose_at_time(target,duration*frame/frames,options)
    original=ext.get_anim_pose_at_time(source,duration*frame/frames,options)
    for side,sign in [('l',-1),('r',1)]:
        wrist=ext.get_bone_pose(corrected,'hand_'+side,world)
        actual=v(unreal.MathLibrary.transform_location(arm_frame,wrist.translation))
        assert length(sub(actual,(48,sign*13.5,-27)))<.1,(side,actual)
        for start,end in [('upperarm','lowerarm'),('lowerarm','hand')]:
            old_len=length(sub(v(ext.get_bone_pose(original,end+'_'+side,world).translation),v(ext.get_bone_pose(original,start+'_'+side,world).translation)))
            new_len=length(sub(v(ext.get_bone_pose(corrected,end+'_'+side,world).translation),v(ext.get_bone_pose(corrected,start+'_'+side,world).translation)))
            assert abs(old_len-new_len)<.01,'Bone length changed'
assert unreal.EditorAssetLibrary.save_loaded_asset(target,False)
(out/'Assets.json').write_text(json.dumps({'mesh':mesh.get_path_name(),'bounds':str(mesh.get_bounds()),'pose':target.get_path_name(),'grip':report},indent=2))
unreal.log('FUSE_BOMB_ASSETS_OK')
