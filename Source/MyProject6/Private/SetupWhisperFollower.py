"""Create Whisper's reusable Blueprint and place one follower in the gameplay map.

Run in Unreal Python after building MyProject6Editor. Existing map packages are
backed up to Saved/WhisperSetup/Backup before saving.
"""
from datetime import datetime
from pathlib import Path
import json
import shutil
import unreal


ROOT = '/Game/NPC/Whisper'
MAP = '/Game/FirstPerson/Lvl_FirstPerson'
LABEL = 'NPC_Whisper'
content = Path(unreal.Paths.project_content_dir())
output = Path(unreal.Paths.project_saved_dir()) / 'WhisperSetup'
backup = output / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')


def backup_file(source):
    if source.is_file():
        destination = backup / source.relative_to(content)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def require_asset(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError('Missing asset: ' + path)
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError('Could not save ' + asset.get_path_name())


mesh = require_asset(ROOT + '/Base_Mesh/SK_Whisper')
skeleton = mesh.get_editor_property('skeleton')
tools = unreal.AssetToolsHelpers.get_asset_tools()
for source in [content / 'FirstPerson/Lvl_FirstPerson.umap',
               content / 'NPC/Whisper/BP_Whisper_Follower.uasset',
               content / 'NPC/Whisper/Animations/BS_Whisper_Locomotion.uasset']:
    backup_file(source)
for source in (content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'):
    backup_file(source)

factory = unreal.BlendSpaceFactory1D()
factory.set_editor_property('target_skeleton', skeleton)
factory.set_editor_property('preview_skeletal_mesh', mesh)
blend_path = ROOT + '/Animations/BS_Whisper_Locomotion'
blend = (unreal.load_asset(blend_path) if unreal.EditorAssetLibrary.does_asset_exist(blend_path)
         else tools.create_asset('BS_Whisper_Locomotion', ROOT + '/Animations', unreal.BlendSpace1D, factory))
parameters = list(blend.get_editor_property('blend_parameters'))
parameters[0].set_editor_property('display_name', 'Speed')
parameters[0].set_editor_property('min', 0.0)
parameters[0].set_editor_property('max', 450.0)
parameters[0].set_editor_property('grid_num', 6)
blend.set_editor_property('blend_parameters', parameters)
samples = []
for suffix, speed in [('Idle1', 0.0), ('Walk1', 150.0), ('Run', 450.0)]:
    animation = require_asset(ROOT + '/Animations/Anim_Whisper_' + suffix)
    if animation.get_editor_property('skeleton') != skeleton:
        raise RuntimeError('Incompatible Whisper animation: ' + suffix)
    sample = unreal.BlendSample()
    sample.set_editor_property('animation', animation)
    sample.set_editor_property('sample_value', unreal.Vector(speed, 0, 0))
    sample.set_editor_property('rate_scale', 1.0)
    samples.append(sample)
blend.set_editor_property('sample_data', samples)
blend.set_editor_property('target_weight_interpolation_speed_per_sec', 5.0)
save(blend)

bp_path = ROOT + '/BP_Whisper_Follower'
bp_factory = unreal.BlueprintFactory()
bp_factory.set_editor_property('parent_class', unreal.WhisperFollower)
bp = (unreal.load_asset(bp_path) if unreal.EditorAssetLibrary.does_asset_exist(bp_path)
      else tools.create_asset('BP_Whisper_Follower', ROOT, unreal.Blueprint, bp_factory))
defaults = unreal.get_default_object(bp.generated_class())
defaults.set_editor_property('locomotion_blend_space', blend)
defaults.rebuild_locomotion_blend_space()
save(blend)
defaults.mesh.set_skeletal_mesh_asset(mesh)
defaults.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=90, roll=0))
defaults.mesh.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)
data = defaults.mesh.get_editor_property('animation_data')
data.set_editor_property('anim_to_play', blend)
data.set_editor_property('saved_looping', True)
data.set_editor_property('saved_playing', True)
defaults.mesh.set_editor_property('animation_data', data)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
save(bp)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world is None or world.get_path_name().split('.')[0] != MAP:
    if not unreal.EditorLoadingAndSavingUtils.load_map(MAP):
        raise RuntimeError('Could not load gameplay map')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = list(actors.get_all_level_actors())
existing = [a for a in all_actors if a.get_actor_label() == LABEL]
if len(existing) > 1:
    raise RuntimeError('Multiple NPC_Whisper actors found')
if existing:
    npc = existing[0]
    if npc.get_class() != bp.generated_class():
        raise RuntimeError('NPC_Whisper is already used by another actor class')
else:
    npc = None
if npc is None:
    start = next(a for a in all_actors if isinstance(a, unreal.PlayerStart))
    origin = start.get_actor_location()
    forward = start.get_actor_forward_vector()
    candidate = origin + forward * 300.0
    hit = unreal.SystemLibrary.line_trace_single(
        world, candidate + unreal.Vector(0, 0, 1000), candidate - unreal.Vector(0, 0, 3000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if not hit:
        raise RuntimeError('No floor found near the player start')
    result = hit.to_dict()
    if not result['blocking_hit']:
        raise RuntimeError('No blocking floor found for Whisper')
    location = result['impact_point'] + unreal.Vector(0, 0, 102)
    rotation = unreal.MathLibrary.find_look_at_rotation(location, origin)
    rotation.pitch = 0
    rotation.roll = 0
    npc = actors.spawn_actor_from_class(bp.generated_class(), location, rotation)
    if npc is None:
        raise RuntimeError('Could not spawn Whisper')
    npc.set_actor_label(LABEL)
    npc.set_folder_path('NPCs')

assert npc.mesh.get_skinned_asset() == mesh
assert npc.get_editor_property('auto_possess_ai') == unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED
assert npc.get_editor_property('locomotion_blend_space') == blend
npc.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=90, roll=0))
if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False):
    raise RuntimeError('Could not save Whisper level placement')
actors.set_selected_level_actors([npc])
report = {'blueprint': bp_path, 'actor': npc.get_path_name(), 'location': str(npc.get_actor_location()),
          'blend_space': blend_path, 'backup': str(backup), 'saved': True}
output.mkdir(parents=True, exist_ok=True)
(output / 'setup.json').write_text(json.dumps(report, indent=2))
unreal.log('WHISPER_SETUP_OK ' + json.dumps(report))
