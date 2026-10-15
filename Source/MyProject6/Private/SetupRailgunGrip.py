"""Bake a left-arm IK correction while preserving the original right-hand animation."""
import math
import json
from pathlib import Path
import unreal

def vec(v): return (v.x, v.y, v.z)
def quat(q): return (q.x, q.y, q.z, q.w)
def add(a,b): return tuple(x+y for x,y in zip(a,b))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def length(a): return math.sqrt(dot(a,a))
def unit(a): return mul(a,1/max(length(a),1e-10))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def inverse(q): return (-q[0],-q[1],-q[2],q[3])
def qmul(a,b):
    v = add(add(mul(b[:3],a[3]),mul(a[:3],b[3])),cross(a[:3],b[:3]))
    return (*v,a[3]*b[3]-dot(a[:3],b[:3]))
def arc(a,b):
    a,b=unit(a),unit(b)
    q=(*cross(a,b),1+dot(a,b))
    return mul(q,1/length(q))

source = unreal.load_asset('/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS')
path = '/Game/Weapons/Railgun/Animations/A_RailgunHold'
target = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(), path)
assert target
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property('optional_skeletal_mesh', unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
raw_options = unreal.AnimPoseEvaluationOptions()
raw_options.set_editor_property('optional_skeletal_mesh', options.optional_skeletal_mesh)
raw_options.set_editor_property('should_retarget', False)
frames = unreal.AnimationLibrary.get_num_frames(source)
duration = unreal.AnimationLibrary.get_sequence_length(source)
ext = unreal.AnimPoseExtensions
world, local = unreal.AnimPoseSpaces.WORLD, unreal.AnimPoseSpaces.LOCAL
tracks = {bone: [[],[],[]] for bone in ['upperarm_l','lowerarm_l','hand_l']}
report = []
for frame in range(frames+1):
    pose = ext.get_anim_pose_at_time(source, duration*frame/frames, options)
    raw_pose = ext.get_anim_pose_at_time(source, duration*frame/frames, raw_options)
    bones = {name:ext.get_bone_pose(pose,name,world) for name in ['clavicle_l','upperarm_l','lowerarm_l','hand_l']}
    grip = ext.get_socket_pose(pose, 'HandGrip_R', world)
    wrist_in_gun = unreal.MathLibrary.inverse_transform_location(grip, bones['hand_l'].translation)
    # The rifle fore-end is about 6 cm wide; the palm rests against its left side.
    wrist_in_gun.x = 4.8
    goal = vec(unreal.MathLibrary.transform_location(grip,wrist_in_gun))
    shoulder, elbow, wrist = [vec(bones[name].translation) for name in ['upperarm_l','lowerarm_l','hand_l']]
    upper_len, lower_len = length(sub(elbow,shoulder)), length(sub(wrist,elbow))
    direction=unit(sub(goal,shoulder))
    distance=length(sub(goal,shoulder))
    assert abs(upper_len-lower_len) < distance < upper_len+lower_len, 'Grip is outside arm reach'
    # Tuck the elbow down/back with two-bone IK, preserving both bone lengths.
    # The forearm keeps its cylindrical anatomy instead of stretching vertices in a shader.
    elbow_guide = vec(unreal.MathLibrary.transform_location(grip, unreal.Vector(26, -12, -24)))
    pole_offset = sub(elbow_guide, shoulder)
    pole=unit(sub(pole_offset,mul(direction,dot(pole_offset,direction))))
    along=(upper_len*upper_len-lower_len*lower_len+distance*distance)/(2*distance)
    height=math.sqrt(max(0,upper_len*upper_len-along*along))
    new_elbow=add(add(shoulder,mul(direction,along)),mul(pole,height))
    upper_q=qmul(arc(sub(elbow,shoulder),sub(new_elbow,shoulder)),quat(bones['upperarm_l'].rotation))
    lower_q=qmul(arc(sub(wrist,elbow),sub(goal,new_elbow)),quat(bones['lowerarm_l'].rotation))
    rotations={
        'upperarm_l':qmul(inverse(quat(bones['clavicle_l'].rotation)),upper_q),
        'lowerarm_l':qmul(inverse(upper_q),lower_q),
        'hand_l':qmul(inverse(lower_q),quat(bones['hand_l'].rotation))}
    for name, values in tracks.items():
        # Raw keys must retain source lengths; the mesh applies retargeting at playback.
        original=ext.get_bone_pose(raw_pose,name,local)
        values[0].append(original.translation)
        values[1].append(unreal.Quat(*rotations[name]))
        values[2].append(original.scale3d)
    if frame in [0,frames//2,frames]: report.append({'frame':frame,'wrist_target':vec(wrist_in_gun)})
controller=target.controller
controller.open_bracket('Fit railgun support hand', False)
try:
    for name,(positions,rotations,scales) in tracks.items():
        assert controller.set_bone_track_keys(name,positions,rotations,scales,False), name
finally:
    controller.close_bracket(False)
for sample in report:
    corrected = ext.get_anim_pose_at_time(target, duration*sample['frame']/frames, options)
    source_pose = ext.get_anim_pose_at_time(source, duration*sample['frame']/frames, options)
    actual_grip = ext.get_socket_pose(corrected, 'HandGrip_R', world)
    actual_wrist = ext.get_bone_pose(corrected, 'hand_l', world)
    actual = vec(unreal.MathLibrary.inverse_transform_location(actual_grip, actual_wrist.translation))
    sample['wrist_actual'] = actual
    assert length(sub(actual, sample['wrist_target'])) < 0.1, f'Grip mismatch: {sample}'
    for start, end in [('upperarm_l', 'lowerarm_l'), ('lowerarm_l', 'hand_l')]:
        original_length = length(sub(vec(ext.get_bone_pose(source_pose,end,world).translation), vec(ext.get_bone_pose(source_pose,start,world).translation)))
        actual_length = length(sub(vec(ext.get_bone_pose(corrected,end,world).translation), vec(ext.get_bone_pose(corrected,start,world).translation)))
        assert abs(original_length - actual_length) < .01, 'Arm bone was stretched'
    original_right = ext.get_bone_pose(source_pose, 'hand_r', world)
    actual_right = ext.get_bone_pose(corrected, 'hand_r', world)
    assert length(sub(vec(actual_right.translation), vec(original_right.translation))) < 0.01
    assert abs(dot(quat(actual_right.rotation), quat(original_right.rotation))) > 0.99999
assert unreal.EditorAssetLibrary.save_loaded_asset(target, False)
Path(unreal.Paths.project_saved_dir(),'Railgun/GripCorrection.json').write_text(json.dumps(report,indent=2))
unreal.log('RAILGUN_GRIP_READY')
