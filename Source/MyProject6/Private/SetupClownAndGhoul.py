"""Make the supplied Clown an NPC, and give the placed Ghoul a stronger hit and full fall.

Run in an isolated Unreal Python commandlet with the editor closed. Modified packages
are backed up. Original character meshes and original animation clips are preserved.
"""
from datetime import datetime
from pathlib import Path
import json
import shutil
import unreal

ROOT = '/Game/Characters/clown_monster'
GHOUL = '/Game/Characters/Ghoul'
LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
E = unreal.AnimPoseExtensions
CONTENT = Path(unreal.Paths.project_content_dir())
OUT = Path(unreal.Paths.project_saved_dir()) / 'ClownGhoul'
BACKUP = OUT / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')
report = {'backup': str(BACKUP)}


def require(path):
    result = unreal.load_asset(path)
    assert result, path
    return result


def backup(path, extension='.uasset'):
    source = CONTENT / (path.split('.')[0].removeprefix('/Game/') + extension)
    if source.is_file():
        destination = BACKUP / source.relative_to(CONTENT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def save(asset):
    assert LIB.save_loaded_asset(asset, False), asset.get_path_name()


def create(name, folder, cls, factory):
    path = folder + '/' + name
    if LIB.does_asset_exist(path):
        backup(path)
        return require(path)
    return TOOLS.create_asset(name, folder, cls, factory)


# Use the artist's complete mesh (body, head, weapon) and its own compatible clips.
mesh = require(ROOT + '/Meshes/base_mesh/SKM_base_mesh')
skeleton = mesh.skeleton
animations = {key: require(ROOT + '/demo/animation/' + name) for key, name in {
    'Idle': 'anim_idle_1', 'Walk': 'anim_Walk', 'Run': 'anim_run_1',
    'Attack': 'anim_attack_1', 'Death': 'anim_death'}.items()}
assert all(a.get_editor_property('skeleton') == skeleton for a in animations.values())
factory = unreal.BlendSpaceFactory1D()
factory.target_skeleton = skeleton
factory.preview_skeletal_mesh = mesh
blend = create('BS_ClownMonster_Locomotion', ROOT + '/Animations', unreal.BlendSpace1D, factory)
parameters = list(blend.get_editor_property('blend_parameters'))
parameters[0].set_editor_property('display_name', 'Speed')
parameters[0].set_editor_property('min', 0.0)
parameters[0].set_editor_property('max', 360.0)
parameters[0].set_editor_property('grid_num', 6)
blend.set_editor_property('blend_parameters', parameters)
samples = []
for name, speed in [('Idle', 0.0), ('Walk', 150.0), ('Run', 360.0)]:
    sample = unreal.BlendSample()
    sample.set_editor_property('animation', animations[name])
    sample.set_editor_property('sample_value', unreal.Vector(speed, 0, 0))
    sample.set_editor_property('rate_scale', 1.0)
    samples.append(sample)
blend.set_editor_property('sample_data', samples)
blend.set_editor_property('target_weight_interpolation_speed_per_sec', 5.0)
factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.WhisperFollower)
bp = create('BP_ClownMonster_NPC', ROOT, unreal.Blueprint, factory)
defaults = unreal.get_default_object(bp.generated_class())
defaults.modify()
defaults.mesh.set_skeletal_mesh_asset(mesh)
defaults.mesh.set_editor_property('relative_location', unreal.Vector(0, 0, -96))
defaults.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=-90, roll=0))
defaults.mesh.set_editor_property('relative_scale3d', unreal.Vector(1, 1, 1))
defaults.capsule_component.set_capsule_size(42, 96)
defaults.character_movement.set_editor_property('max_walk_speed', 360.0)
defaults.set_editor_property('max_health', 150.0)
defaults.set_editor_property('locomotion_blend_space', blend)
defaults.rebuild_locomotion_blend_space()
defaults.set_editor_property('contact_attack_animation', animations['Attack'])
defaults.set_editor_property('contact_attack_play_rate', 1.0)
defaults.set_editor_property('death_animation', animations['Death'])
defaults.set_editor_property('hit_reaction_animation', None)
defaults.set_editor_property('contact_interaction_radius', 140.0)
defaults.set_editor_property('contact_energy_drain_per_second', 20.0)
defaults.get_component_by_class(unreal.NPCHealthBarComponent).set_editor_property('head_clearance', 38.0)
defaults.mesh.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)
data = defaults.mesh.get_editor_property('animation_data')
data.set_editor_property('anim_to_play', blend)
data.set_editor_property('saved_looping', True)
data.set_editor_property('saved_playing', True)
defaults.mesh.set_editor_property('animation_data', data)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
save(blend)
save(bp)
report['clown_blueprint'] = bp.get_path_name()

# Bake the supplied complete fall to Ghoul's five-spine UE5 rig. Transfer animated
# rotations relative to each skeleton's bind pose in component space, retaining
# Ghoul's limb lengths. Additional UE5 spine/neck bones keep their bind transforms.
ghoul_mesh = require(GHOUL + '/Mesh/SK_Ghoul_UE5')
ghoul_skeleton = ghoul_mesh.skeleton
target_ref = E.get_reference_pose(ghoul_skeleton)
source_ref = E.get_reference_pose(skeleton)
bones = [str(b) for b in E.get_bone_names(target_ref)]
source_bones = {str(b) for b in E.get_bone_names(source_ref)}
ghoul_component = unreal.get_default_object(unreal.ChasingNPC).mesh
parents = {b: str(ghoul_component.get_parent_bone(b)) for b in bones}
ref_local = {b: E.get_bone_pose(target_ref, b, unreal.AnimPoseSpaces.LOCAL) for b in bones}
ref_world = {b: E.get_bone_pose(target_ref, b, unreal.AnimPoseSpaces.WORLD) for b in bones}
source_world = {b: E.get_bone_pose(source_ref, b, unreal.AnimPoseSpaces.WORLD) for b in source_bones}
source_pelvis = E.get_bone_pose(source_ref, 'pelvis', unreal.AnimPoseSpaces.LOCAL).translation
height_ratio = ref_local['pelvis'].translation.z / source_pelvis.z
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property('should_retarget', False)
source = animations['Death']
frames = round(source.get_play_length() * 30)
tracks = {b: [] for b in bones}
for frame in range(frames + 1):
    pose = E.get_anim_pose_at_time(source, min(frame / 30.0, source.get_play_length()), options)
    rotations = {}
    for bone in bones:
        parent_rotation = rotations.get(parents[bone], unreal.Quat())
        reference = ref_local[bone]
        if bone in source_bones:
            source_rotation = E.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).rotation
            rotation = source_rotation * source_world[bone].rotation.inversed() * ref_world[bone].rotation
        else:
            rotation = parent_rotation * reference.rotation
        rotations[bone] = rotation
        local_rotation = parent_rotation.inversed() * rotation
        translation = reference.translation
        if bone == 'pelvis':
            animated = E.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL).translation
            translation = reference.translation + (animated - source_pelvis) * height_ratio
        tracks[bone].append((translation, local_rotation, reference.scale3d))

factory = unreal.AnimSequenceFactory()
factory.target_skeleton = ghoul_skeleton
factory.preview_skeletal_mesh = ghoul_mesh
death = create('A_Ghoul_Death_FullFall', GHOUL + '/Animations', unreal.AnimSequence, factory)
controller = death.controller
controller.open_bracket('Retarget full fall with a grounded final pose', False)
try:
    controller.set_frame_rate(unreal.FrameRate(30, 1), False)
    controller.set_number_of_frames(unreal.FrameNumber(frames), False)
    controller.remove_all_bone_tracks(False)
    for bone in bones:
        controller.add_bone_curve(bone, False)
        keys = tracks[bone]
        assert controller.set_bone_track_keys(bone, [x[0] for x in keys], [x[1] for x in keys], [x[2] for x in keys], False)
finally:
    controller.close_bracket(False)
save(death)
report['ghoul_death'] = death.get_path_name()
end = E.get_anim_pose_at_time(death, death.get_play_length(), options)
report['ghoul_final_bones'] = {b: str(E.get_bone_pose(end, b, unreal.AnimPoseSpaces.WORLD).translation)
    for b in ['pelvis', 'spine_05', 'head', 'foot_l', 'foot_r']}
assert E.get_bone_pose(end, 'head', unreal.AnimPoseSpaces.WORLD).translation.z < 40
assert E.get_bone_pose(end, 'pelvis', unreal.AnimPoseSpaces.WORLD).translation.z < 30

# Add a selectable spawn alias without changing the current default or existing aliases.
spawn_bp = require('/Game/NPC/Spawning/BP_NPCSpawnPoint')
backup('/Game/NPC/Spawning/BP_NPCSpawnPoint')
spawn_defaults = unreal.get_default_object(spawn_bp.generated_class())
types = dict(spawn_defaults.get_editor_property('npc_types'))
types['ClownMonster'] = bp.generated_class()
spawn_defaults.set_editor_property('npc_types', types)
unreal.BlueprintEditorLibrary.compile_blueprint(spawn_bp)
save(spawn_bp)

assert unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
backup(LEVEL, '.umap')
external = CONTENT / '__ExternalActors__/FirstPerson/Lvl_FirstPerson'
for source_file in external.rglob('*.uasset'):
    destination = BACKUP / source_file.relative_to(CONTENT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_file, destination)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
ghouls = [a for a in actors if a.get_actor_label() == 'NPC_PlayerChaser']
assert len(ghouls) == 1 and isinstance(ghouls[0], unreal.ChasingNPC)
npc = ghouls[0]
npc.modify()
reaction = npc.get_component_by_class(unreal.NPCHitReactionComponent)
reaction.modify()
reaction.set_editor_property('forward_bend_angle', 65.0)
reaction.set_editor_property('stagger_duration', .40)
npc.set_editor_property('death_animation', death)
npc.set_editor_property('death_animation_play_rate', 1.0)
for actor in actors:
    if isinstance(actor, unreal.NPCSpawnPoint):
        types = dict(actor.get_editor_property('npc_types'))
        types['ClownMonster'] = bp.generated_class()
        actor.modify()
        actor.set_editor_property('npc_types', types)
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
report['player_chaser_class'] = npc.get_class().get_path_name()
report['player_chaser_mesh'] = npc.mesh.skeletal_mesh_asset.get_path_name()
report['bend_degrees'] = 65
report['stagger_seconds'] = .4
(OUT / 'Setup.json').write_text(json.dumps(report, indent=2))
unreal.log('CLOWN_GHOUL_SETUP_OK ' + json.dumps(report))
