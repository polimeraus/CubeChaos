"""Create BP_DemonImp_NPC from the Fab model, using existing NPC gameplay.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi.
No gameplay map or original Fab package is modified.
"""
import json
from pathlib import Path
import unreal

ROOT = '/Game/Characters/DemonImp'
FAB = '/Game/Fab/Demon_Imp_-_Hand_Painted_Series/demon_imp/SkeletalMeshes'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
OUT = Path(unreal.Paths.project_saved_dir()) / 'DemonImp'
OUT.mkdir(parents=True, exist_ok=True)


def require(path):
    asset = unreal.load_asset(path)
    assert asset, 'Missing asset: ' + path
    return asset


def save(asset):
    assert LIB.save_loaded_asset(asset, False), asset.get_path_name()


def create(name, folder, cls, factory):
    package = ROOT + ('/' + folder if folder else '')
    path = package + '/' + name
    return require(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, package, cls, factory)


mesh = require(FAB + '/demon_imp')
skeleton = mesh.skeleton
source = require(FAB + '/demon_imp_Anim')
assert source.get_editor_property('skeleton') == skeleton
assert len(mesh.materials) == 2, 'Body and pupils must be combined into the same skinned mesh'

# The artist supplied one take at 30 fps. These ranges isolate complete actions;
# the reference T-pose, jumps and unrelated demonstration motions are excluded.
ranges = {'Idle': (32, 82), 'Walk': (351, 372), 'Run': (231, 246),
          'Attack': (785, 815), 'Death': (975, 1035)}
ext = unreal.AnimPoseExtensions
reference = ext.get_reference_pose(skeleton)
bones = list(ext.get_bone_names(reference))
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property('should_retarget', False)
animations = {}
for name, (first, last) in ranges.items():
    factory = unreal.AnimSequenceFactory()
    factory.target_skeleton = skeleton
    factory.preview_skeletal_mesh = mesh
    animation = create('A_DemonImp_' + name, 'Animations', unreal.AnimSequence, factory)
    assert animation.get_editor_property('skeleton') == skeleton
    poses = [ext.get_anim_pose_at_time(source, frame / 30.0, options) for frame in range(first, last + 1)]
    if name != 'Death':
        poses[-1] = poses[0]
    controller = animation.controller
    controller.open_bracket('Extract Demon Imp ' + name, False)
    try:
        controller.set_frame_rate(unreal.FrameRate(30, 1), False)
        controller.set_number_of_frames(unreal.FrameNumber(last - first), False)
        controller.remove_all_bone_tracks(False)
        for bone in bones:
            transforms = [ext.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL) for pose in poses]
            controller.add_bone_curve(bone, False)
            assert controller.set_bone_track_keys(bone, [p.translation for p in transforms],
                                                  [p.rotation for p in transforms], [p.scale3d for p in transforms], False)
    finally:
        controller.close_bracket(False)
    save(animation)
    animations[name] = animation

factory = unreal.BlendSpaceFactory1D()
factory.target_skeleton = skeleton
factory.preview_skeletal_mesh = mesh
blend = create('BS_DemonImp_Locomotion', 'Animations', unreal.BlendSpace1D, factory)
assert blend.get_editor_property('skeleton') == skeleton
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

# WhisperFollower is the project's existing skeleton-independent NPC implementation.
# It already drives blend spaces, pursues the player, drains energy, staggers and dies.
factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.WhisperFollower)
bp = create('BP_DemonImp_NPC', '', unreal.Blueprint, factory)
defaults = unreal.get_default_object(bp.generated_class())
defaults.mesh.set_skeletal_mesh_asset(mesh)
defaults.mesh.set_editor_property('relative_location', unreal.Vector(0, 0, -74))
defaults.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=-90, roll=0))
# Fab's GLB is authored in a large unit scale. Scale the component, keeping the
# mesh's bind pose and skeleton untouched (import scaling can change them separately).
defaults.mesh.set_editor_property('relative_scale3d', unreal.Vector(.012, .012, .012))
defaults.capsule_component.set_capsule_size(42.0, 74.0)
defaults.character_movement.set_editor_property('max_walk_speed', 360.0)
defaults.set_editor_property('max_health', 100.0)
defaults.set_editor_property('locomotion_blend_space', blend)
defaults.rebuild_locomotion_blend_space()
defaults.set_editor_property('contact_attack_animation', animations['Attack'])
defaults.set_editor_property('contact_attack_play_rate', 1.0)
defaults.set_editor_property('death_animation', animations['Death'])
defaults.set_editor_property('hit_reaction_animation', None)
defaults.set_editor_property('contact_interaction_radius', 140.0)
defaults.set_editor_property('contact_energy_drain_per_second', 20.0)
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
          'parent': 'WhisperFollower', 'animation_source_frames': ranges,
          'health': 100, 'speed_cm_s': 360, 'capsule_radius': 42, 'capsule_half_height': 74,
          'animations': {key: value.get_path_name() for key, value in animations.items()},
          'map_modified': False}
(OUT / 'Setup.json').write_text(json.dumps(report, indent=2))
unreal.log('DEMON_IMP_SETUP_OK ' + json.dumps(report))
