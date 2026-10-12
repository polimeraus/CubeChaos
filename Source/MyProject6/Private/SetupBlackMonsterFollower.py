"""Import Black Monster and build its placeable follower Blueprint.

Run using UnrealEditor-Cmd -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi.
Source FBXs are recovered under Saved/BlackMonster/Source/Extracted.
Only assets in Characters/black-monster are edited; no level is changed.
"""
from datetime import datetime
from pathlib import Path
import json
import math
import shutil
import unreal

ROOT = '/Game/Characters/black-monster'
OUTPUT = Path(unreal.Paths.project_saved_dir()) / 'BlackMonster'
SOURCE = OUTPUT / 'Source/Extracted'
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EXT = unreal.AnimPoseExtensions
LOCAL = unreal.AnimPoseSpaces.LOCAL
WORLD = unreal.AnimPoseSpaces.WORLD
backup = OUTPUT / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')
content = Path(unreal.Paths.project_content_dir())
for existing in (content / 'Characters/black-monster').rglob('*.uasset'):
    if 'textures' not in existing.parts:
        destination = backup / existing.relative_to(content)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(existing, destination)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError('Could not save ' + asset.get_path_name())


def require(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError('Missing asset: ' + path)
    return asset


def create(name, subfolder, asset_class, factory):
    path = ROOT + '/' + subfolder + '/' + name if subfolder else ROOT + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(path)
    asset = TOOLS.create_asset(name, path.rsplit('/', 1)[0], asset_class, factory)
    if asset is None:
        raise RuntimeError('Could not create ' + path)
    return asset


unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')


def import_fbx(suffix, name, skeleton=None):
    source = next(SOURCE.rglob('*' + suffix + '_withSkin.fbx'))
    options = unreal.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_ANIMATION if skeleton else unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    options.set_editor_property('import_as_skeletal', True)
    options.set_editor_property('import_mesh', skeleton is None)
    options.set_editor_property('import_animations', skeleton is not None)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('create_physics_asset', False)
    if skeleton:
        options.skeleton = skeleton
        options.anim_sequence_import_data.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = ROOT + ('/Animations' if skeleton else '/Mesh')
    task.destination_name = name
    task.automated = True
    task.replace_existing = False
    task.save = False
    task.options = options
    TOOLS.import_asset_tasks([task])
    unreal.EditorAssetLibrary.save_directory(ROOT, False, True)
    return require(task.destination_path + '/' + name)


mesh_path = ROOT + '/Mesh/SK_BlackMonster'
mesh = (require(mesh_path) if unreal.EditorAssetLibrary.does_asset_exist(mesh_path)
        else import_fbx('Walking', 'SK_BlackMonster'))
skeleton = mesh.get_editor_property('skeleton')
if skeleton is None:
    raise RuntimeError('Black Monster mesh has no saved skeleton')
save(skeleton)

# Use the downloaded model's own motion and skeleton, with translation removed
# so CharacterMovement, collision and navigation remain responsible for movement.
animations = {}
for name, suffix in [('Walk', 'Walking'), ('Run', 'Lean_Forward_Sprint_inplace')]:
    path = ROOT + '/Animations/Anim_BlackMonster_' + name
    animation = (require(path) if unreal.EditorAssetLibrary.does_asset_exist(path)
                 else import_fbx(suffix, 'Anim_BlackMonster_' + name, skeleton))
    animations[name] = animation
    if animation.get_editor_property('skeleton') != skeleton:
        raise RuntimeError('Animation skeleton mismatch: ' + name)

reference = EXT.get_reference_pose(skeleton)
bones = list(EXT.get_bone_names(reference))
root_position = EXT.get_bone_pose(reference, 'Hips', LOCAL).translation


def sample(animation, seconds):
    return EXT.get_anim_pose_at_time(animation, seconds, unreal.AnimPoseEvaluationOptions())


for animation in animations.values():
    frames = unreal.AnimationLibrary.get_num_frames(animation)
    length = animation.get_play_length()
    poses = [EXT.get_bone_pose(sample(animation, length * i / frames), 'Hips', LOCAL) for i in range(frames + 1)]
    controller = animation.controller
    controller.open_bracket('Keep Black Monster locomotion in place', False)
    controller.set_bone_track_keys('Hips', [unreal.Vector(root_position.x, root_position.y, p.translation.z) for p in poses],
                                   [p.rotation for p in poses], [p.scale3d for p in poses], False)
    controller.close_bracket(False)
    save(animation)


def vector_normal(v):
    n = math.sqrt(sum(c*c for c in v))
    return tuple(c / max(n, 1e-8) for c in v)


def between(a, b):
    a, b = vector_normal(a), vector_normal(b)
    q = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0], 1 + sum(x*y for x, y in zip(a, b)))
    return unreal.Quat(*vector_normal(q))


def quat_mul(a, b):
    return unreal.Quat(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                       a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                       a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                       a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z)


def aim(pose, bone, child, direction):
    transform = EXT.get_bone_pose(pose, bone, WORLD)
    end = EXT.get_bone_pose(pose, child, WORLD).translation
    delta = end - transform.translation
    transform.rotation = quat_mul(between((delta.x, delta.y, delta.z), direction), transform.rotation)
    return EXT.set_bone_pose(pose, transform, bone, WORLD)


def rotate_world(pose, bone, degrees):
    transform = EXT.get_bone_pose(pose, bone, WORLD)
    transform.rotation = quat_mul(unreal.Rotator(pitch=0, yaw=degrees, roll=0).quaternion(), transform.rotation)
    return EXT.set_bone_pose(pose, transform, bone, WORLD)


def rotate_axis(pose, bone, axis, degrees):
    angle = math.radians(degrees) / 2
    q = unreal.Quat(*(c * math.sin(angle) for c in axis), math.cos(angle))
    transform = EXT.get_bone_pose(pose, bone, WORLD)
    transform.rotation = quat_mul(q, transform.rotation)
    return EXT.set_bone_pose(pose, transform, bone, WORLD)


def xyz(v):
    return (v.x, v.y, v.z)


def rotate_vector(q, v):
    value = quat_mul(quat_mul(q, unreal.Quat(*xyz(v), 0)), unreal.Quat(-q.x, -q.y, -q.z, q.w))
    return unreal.Vector(value.x, value.y, value.z)


def pin_feet(pose):
    # Two-bone IK preserves the planted toes while the hips shift into each blow.
    for side in ['Left', 'Right']:
        thigh, shin, foot = side + 'UpLeg', side + 'Leg', side + 'Foot'
        hip = EXT.get_bone_pose(pose, thigh, WORLD).translation
        knee = EXT.get_bone_pose(pose, shin, WORLD).translation
        ankle = EXT.get_bone_pose(pose, foot, WORLD).translation
        target = EXT.get_bone_pose(reference, foot, WORLD)
        length1 = (knee-hip).length()
        length2 = (ankle-knee).length()
        distance = min((target.translation-hip).length(), length1 + length2 - .001)
        along = unreal.Vector(*vector_normal(xyz(target.translation-hip)))
        pole = EXT.get_bone_pose(reference, shin, WORLD).translation - hip
        projection = sum(x*y for x, y in zip(xyz(pole), xyz(along)))
        perpendicular = unreal.Vector(*vector_normal(xyz(pole-along*projection)))
        adjacent = (length1*length1-length2*length2+distance*distance)/(2*distance)
        bend = hip + along*adjacent + perpendicular*math.sqrt(max(0, length1*length1-adjacent*adjacent))
        pose = aim(pose, thigh, shin, xyz(bend-hip))
        knee = EXT.get_bone_pose(pose, shin, WORLD).translation
        pose = aim(pose, shin, foot, xyz(target.translation-knee))
        transform = EXT.get_bone_pose(pose, foot, WORLD)
        transform.rotation = target.rotation
        pose = EXT.set_bone_pose(pose, transform, foot, WORLD)
    return pose


def smooth_keys(t, keys):
    for (start, a), (end, b) in zip(keys, keys[1:]):
        if t <= end:
            blend = max(0, min(1, (t-start)/(end-start)))
            blend = blend*blend*(3-2*blend)
            return tuple(x+(y-x)*blend for x, y in zip(a, b))
    return keys[-1][1]


def idle_pose(t=0):
    pose = EXT.get_reference_pose(skeleton)
    transform = EXT.get_bone_pose(pose, 'Spine02', LOCAL)
    transform.translation = transform.translation + unreal.Vector(0, 0, math.sin(t*math.tau)*.5)
    pose = EXT.set_bone_pose(pose, transform, 'Spine02', LOCAL)
    pose = aim(pose, 'RightArm', 'RightForeArm', (-.40, .24, -.72))
    pose = aim(pose, 'RightForeArm', 'RightHand', (-.15, .50, -.75))
    pose = aim(pose, 'LeftArm', 'LeftForeArm', (.50, .25, -.75))
    pose = aim(pose, 'LeftForeArm', 'LeftHand', (-.15, .65, -.50))
    return pose


def blend_pose(a, b, alpha):
    for bone in bones:
        start, end = EXT.get_bone_pose(a, bone, LOCAL), EXT.get_bone_pose(b, bone, LOCAL)
        q1, q2 = start.rotation, end.rotation
        first, second = (q1.x, q1.y, q1.z, q1.w), (q2.x, q2.y, q2.z, q2.w)
        if sum(x*y for x, y in zip(first, second)) < 0:
            second = tuple(-v for v in second)
        start.rotation = unreal.Quat(*vector_normal(tuple(x+(y-x)*alpha for x, y in zip(first, second))))
        start.translation = start.translation + (end.translation-start.translation)*alpha
        a = EXT.set_bone_pose(a, start, bone, LOCAL)
    return a


whisper_attack = require('/Game/Characters/Whisper/Animations/Anim_Whisper_Attack2')
whisper_start = sample(whisper_attack, 0)


def whisper_strike(seconds, mirrored=False):
    """Transfer Whisper's shoulder-led slash, retaining this rig's bone lengths.

    World-space anatomical directions avoid copying incompatible bind rotations.
    The opposite slash reflects the whole performance, including its body recoil
    and counterbalancing arm, rather than moving only the other wrist.
    """
    source = sample(whisper_attack, seconds)
    mirror = -1 if mirrored else 1

    def position(bone, sampled=source):
        p = EXT.get_bone_pose(sampled, bone, WORLD).translation
        return unreal.Vector(p.x*mirror, p.y, p.z)

    def direction(parent, child):
        return position(child)-position(parent)

    pose = idle_pose()
    shoulders = direction('upperarm_r', 'upperarm_l')*mirror
    turn = math.degrees(math.atan2(shoulders.y, shoulders.x))
    spine = direction('pelvis', 'head')
    lean = math.degrees(math.atan2(spine.y, spine.z))
    displacement = position('pelvis')-position('pelvis', whisper_start)
    hip = EXT.get_bone_pose(pose, 'Hips', LOCAL)
    hip.translation = hip.translation + unreal.Vector(displacement.x*.6, displacement.y*.55,
                                                       displacement.z*.6-9)
    pose = EXT.set_bone_pose(pose, hip, 'Hips', LOCAL)
    pose = rotate_world(pose, 'Hips', turn*.15)
    pose = pin_feet(pose)
    pose = rotate_world(pose, 'Spine02', turn*.35)
    pose = rotate_world(pose, 'Spine', turn*.5)
    pose = rotate_axis(pose, 'Spine02', (1,0,0), -lean*.65)
    pose = rotate_world(pose, 'Head', -turn*.3)

    for source_side, target_side in [('r', 'Left' if mirrored else 'Right'),
                                     ('l', 'Right' if mirrored else 'Left')]:
        upper, lower, wrist = ('upperarm_'+source_side, 'lowerarm_'+source_side, 'hand_'+source_side)
        pose = aim(pose, target_side+'Arm', target_side+'ForeArm', xyz(direction(upper, lower)))
        pose = aim(pose, target_side+'ForeArm', target_side+'Hand', xyz(direction(lower, wrist)))
        hand = EXT.get_bone_pose(pose, target_side+'Hand', WORLD)
        current = rotate_vector(hand.rotation, unreal.Vector(0,-1,0))
        claw = direction(wrist, 'index_01_'+source_side)
        hand.rotation = quat_mul(between(xyz(current), xyz(claw)), hand.rotation)
        pose = EXT.set_bone_pose(pose, hand, target_side+'Hand', WORLD)
    return pose


def claw_pose(t):
    # Keep the source's acceleration through the overhead arc: independently
    # easing every wrist key used to stop the arm in mid-swing. Each one-second
    # slash now has a long wind-up, a fast downswing and a full cross-body finish.
    mirrored = t >= .5
    phase = (t*2) % 1
    timing = [(0,.12), (.24,.433), (.32,.542), (.42,.65), (.58,.84), (.76,1.04), (1,1.30)]
    for (start, a), (end, b) in zip(timing, timing[1:]):
        if phase <= end:
            seconds = a+(b-a)*(phase-start)/(end-start)
            break
    pose = whisper_strike(seconds, mirrored)
    if phase > .76:
        # Flow out of the follow-through into the opposite shoulder's wind-up.
        # At the loop boundary both poses match exactly, without an idle pause.
        transition = (phase-.76)/.24
        next_pose = whisper_strike(.12*transition, not mirrored)
        pose = blend_pose(pose, next_pose, transition*transition*(3-2*transition))
        pose = pin_feet(pose)
    return pose


def make_pose(kind, t):
    if kind == 'Idle':
        return idle_pose(t)
    pose = idle_pose()
    if kind == 'Claw':
        return claw_pose(t if t < 1 else 0)
    if kind == 'Hit':
        recoil = smooth_keys(t, [(0,(0,)), (.22,(1,)), (.50,(.7,)), (1,(0,))])[0]
        hip = EXT.get_bone_pose(pose, 'Hips', LOCAL)
        hip.translation = hip.translation + unreal.Vector(0, -5*recoil, -4*recoil)
        pose = EXT.set_bone_pose(pose, hip, 'Hips', LOCAL)
        pose = pin_feet(pose)
        pose = rotate_axis(pose, 'Spine02', (1,0,0), 12*recoil)
        pose = rotate_axis(pose, 'Head', (1,0,0), 10*recoil)
        flinch = pose
        for side, sign in [('Left',1), ('Right',-1)]:
            flinch = aim(flinch, side+'Arm', side+'ForeArm', (sign*.7, -.35, .1))
            flinch = aim(flinch, side+'ForeArm', side+'Hand', (sign*.25, .55, .4))
        return blend_pose(pose, flinch, recoil)
    if kind == 'Death':
        tilt, height = smooth_keys(t, [(0,(0,root_position.z)), (.2,(28,root_position.z)),
                                       (.58,(76,40)), (.78,(92,13)), (.9,(87,16)), (1,(90,14))])
        pose = rotate_axis(pose, 'Hips', (1,0,0), tilt)
        hip = EXT.get_bone_pose(pose, 'Hips', LOCAL)
        hip.translation = unreal.Vector(root_position.x, root_position.y, height)
        pose = EXT.set_bone_pose(pose, hip, 'Hips', LOCAL)
        for side, sign in [('Left',1), ('Right',-1)]:
            loose = min(1,t/.3)
            bent = aim(pose, side+'Arm', side+'ForeArm', (sign*.7, .3, -.12))
            bent = aim(bent, side+'ForeArm', side+'Hand', (sign*.5, .8, .05))
            pose = blend_pose(pose, bent, loose)
        return pose
    raise ValueError(kind)


for kind, frames in [('Idle', 60), ('Claw', 60), ('Hit', 12), ('Death', 30)]:
    factory = unreal.AnimSequenceFactory()
    factory.target_skeleton = skeleton
    factory.preview_skeletal_mesh = mesh
    animation = create('Anim_BlackMonster_' + kind, 'Animations', unreal.AnimSequence, factory)
    poses = [make_pose(kind, i/frames) for i in range(frames+1)]
    controller = animation.controller
    controller.open_bracket('Author Black Monster ' + kind, False)
    try:
        controller.set_frame_rate(unreal.FrameRate(30, 1), False)
        controller.set_number_of_frames(unreal.FrameNumber(frames), False)
        controller.remove_all_bone_tracks(False)
        for bone in bones:
            transforms = [EXT.get_bone_pose(p, bone, LOCAL) for p in poses]
            controller.add_bone_curve(bone, False)
            if not controller.set_bone_track_keys(bone, [p.translation for p in transforms],
                                                  [p.rotation for p in transforms], [p.scale3d for p in transforms], False):
                raise RuntimeError('Failed animation bone: ' + str(bone))
    finally:
        controller.close_bracket(False)
    save(animation)
    animations[kind] = animation

# Reuse the imported PBR textures in one character material.
material = create('M_BlackMonster', 'Materials', unreal.Material, unreal.MaterialFactoryNew())
unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
for index, (texture_name, prop, sampler) in enumerate([
    ('texture_0', unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
    ('Meshy_AI_The_Hollow_Stalker_biped_texture_0_normal', unreal.MaterialProperty.MP_NORMAL, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
    ('Meshy_AI_The_Hollow_Stalker_biped_texture_0_roughness', unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
    ('Meshy_AI_The_Hollow_Stalker_biped_texture_0_metallic', unreal.MaterialProperty.MP_METALLIC, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
]):
    texture = require(ROOT + '/textures/' + texture_name)
    # Keep downloaded texture packages intact; sampler follows each texture's color space.
    if prop in (unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_METALLIC) and texture.get_editor_property('srgb'):
        sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, index * 220)
    node.texture = texture
    node.sampler_type = sampler
    unreal.MaterialEditingLibrary.connect_material_property(node, 'RGB' if index < 2 else 'R', prop)
material.set_editor_property('used_with_skeletal_mesh', True)
unreal.MaterialEditingLibrary.recompile_material(material)
save(material)
materials = list(mesh.get_editor_property('materials'))
for slot in materials:
    slot.set_editor_property('material_interface', material)
mesh.set_editor_property('materials', materials)
save(mesh)

factory = unreal.BlendSpaceFactory1D()
factory.target_skeleton = skeleton
factory.preview_skeletal_mesh = mesh
blend = create('BS_BlackMonster_Locomotion', 'Animations', unreal.BlendSpace1D, factory)
parameters = list(blend.get_editor_property('blend_parameters'))
parameters[0].set_editor_property('display_name', 'Speed')
parameters[0].set_editor_property('min', 0.0)
parameters[0].set_editor_property('max', 450.0)
parameters[0].set_editor_property('grid_num', 6)
blend.set_editor_property('blend_parameters', parameters)
samples = []
for name, speed in [('Idle', 0), ('Walk', 150), ('Run', 450)]:
    entry = unreal.BlendSample()
    entry.set_editor_property('animation', animations[name])
    entry.set_editor_property('sample_value', unreal.Vector(speed, 0, 0))
    entry.set_editor_property('rate_scale', 1.0)
    samples.append(entry)
blend.set_editor_property('sample_data', samples)
blend.set_editor_property('target_weight_interpolation_speed_per_sec', 5.0)

factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.WhisperFollower)
bp = create('BP_BlackMonster_Follower', '', unreal.Blueprint, factory)
defaults = unreal.get_default_object(bp.generated_class())
defaults.set_editor_property('locomotion_blend_space', blend)
defaults.rebuild_locomotion_blend_space()
defaults.set_editor_property('contact_attack_animation', animations['Claw'])
defaults.set_editor_property('contact_attack_play_rate', 1.0)
defaults.set_editor_property('death_animation', animations['Death'])
defaults.set_editor_property('hit_reaction_animation', animations['Hit'])
defaults.get_component_by_class(unreal.NPCHealthBarComponent).set_editor_property('head_clearance', 62.0)
reaction = defaults.get_component_by_class(unreal.NPCHitReactionComponent)
reaction.set_editor_property('stagger_distance', 28.0)
reaction.set_editor_property('stagger_duration', .42)
reaction.set_editor_property('forward_bend_angle', 70.0)
reaction.set_editor_property('death_horizontal_speed', 550.0)
reaction.set_editor_property('death_upward_speed', 360.0)
defaults.set_editor_property('contact_interaction_radius', 140.0)
defaults.set_editor_property('contact_energy_drain_per_second', 20.0)
defaults.mesh.set_skeletal_mesh_asset(mesh)
defaults.mesh.set_editor_property('relative_location', unreal.Vector(0, 0, -100))
defaults.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=-90, roll=0))
defaults.mesh.set_editor_property('relative_scale3d', unreal.Vector(1.25, 1.25, 1.25))
defaults.mesh.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)
data = defaults.mesh.get_editor_property('animation_data')
data.set_editor_property('anim_to_play', blend)
data.set_editor_property('saved_looping', True)
data.set_editor_property('saved_playing', True)
defaults.mesh.set_editor_property('animation_data', data)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
save(blend)
save(bp)
report = {'blueprint': bp.get_path_name(), 'mesh': mesh.get_path_name(),
          'animations': {k: v.get_path_name() for k, v in animations.items()},
          'backup': str(backup), 'saved': True}
(OUTPUT / 'setup.json').write_text(json.dumps(report, indent=2))
unreal.log('BLACK_MONSTER_SETUP_OK ' + json.dumps(report))
