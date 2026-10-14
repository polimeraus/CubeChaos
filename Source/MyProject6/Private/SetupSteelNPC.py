"""Create a persistent Steel NPC and bake missing motions onto its skeleton."""
import json
from pathlib import Path
import unreal

OUT = (Path(unreal.Paths.project_saved_dir()) / 'SteelNPC').resolve()
OUT.mkdir(parents=True, exist_ok=True)
ROOT = '/Game/Characters/ParagonSteel'
CRUNCH = '/Game/Characters/ParagonCrunch/Characters/Heroes/Crunch'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def require(path):
    obj = unreal.load_asset(path)
    assert obj, path
    return obj


def create(name, folder, cls, factory):
    return require(folder+'/'+name) if LIB.does_asset_exist(folder+'/'+name) else TOOLS.create_asset(name, folder, cls, factory)


def save(obj):
    assert LIB.save_loaded_asset(obj, False), obj.get_path_name()


source = require(CRUNCH + '/Meshes/Crunch')
target = require(ROOT + '/Characters/Heroes/Steel/Meshes/Steel')
rigs = []
report = {'rigs': [], 'npcs': []}
for name, mesh in [('Crunch', source), ('Steel', target)]:
    rig = create('IK_' + name + '_NPC', ROOT+'/NPC/Retargeting', unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    controller = unreal.IKRigController.get_controller(rig)
    assert controller.set_skeletal_mesh(mesh)
    assert controller.apply_auto_generated_retarget_definition(), name + ' auto chains'
    assert controller.apply_auto_fbik(), name + ' auto IK'
    rigs.append(rig)
    report['rigs'].append({'name': name, 'chains': [str(c) for c in controller.get_retarget_chains()]})
    save(rig)
retargeter = create('RTG_Crunch_To_Steel_NPC', ROOT+'/NPC/Retargeting', unreal.IKRetargeter, unreal.IKRetargetFactory())
controller = unreal.IKRetargeterController.get_controller(retargeter)
for side, rig, mesh in [(unreal.RetargetSourceOrTarget.SOURCE, rigs[0], source), (unreal.RetargetSourceOrTarget.TARGET, rigs[1], target)]:
    controller.set_ik_rig(side, rig)
    controller.set_preview_mesh(side, mesh)
controller.add_default_ops()
controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, rigs[0])
controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, rigs[1])
controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
controller.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
save(retargeter)
(OUT/'Retargeting.json').write_text(json.dumps(report, indent=2))
names = {'idle': 'Idle_Combat', 'run': 'Jog_Fwd', 'attack': 'Ability_GutPunch', 'death': 'Death_A'}
inputs = unreal.IKRetargetBatchOperationInputs()
inputs.set_editor_property('assets_to_retarget', [LIB.find_asset_data(CRUNCH+'/Animations/'+v) for v in names.values()])
inputs.set_editor_property('source_mesh', source)
inputs.set_editor_property('target_mesh', target)
inputs.set_editor_property('ik_retarget_asset', retargeter)
inputs.set_editor_property('prefix', 'A_Steel_')
inputs.set_editor_property('target_path', ROOT+'/NPC/Animations')
inputs.set_editor_property('include_referenced_assets', False)
inputs.set_editor_property('overwrite_existing_files', True)
if not all(LIB.does_asset_exist(ROOT+'/NPC/Animations/A_Steel_'+v) for v in names.values()):
    result = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    assert len(result) == 4, str(result)
animations = {k: require(ROOT+'/NPC/Animations/A_Steel_'+v) for k,v in names.items()}
# Steel's deployable shield has no counterpart on Crunch. Keep its original
# folded presentation, baking local accessory transforms into every motion.
ext = unreal.AnimPoseExtensions
accessory_pose = ext.get_anim_pose_at_time(require(ROOT+'/Characters/Heroes/Steel/Animations/FrontEndPose'), 0., unreal.AnimPoseEvaluationOptions())
accessories = [b for b in ext.get_bone_names(accessory_pose) if str(b).startswith(('sheild_', 'panel', 'loarm_shield_'))]
for anim in animations.values():
    assert anim.get_editor_property('skeleton') == target.skeleton
    assert anim.get_editor_property('additive_anim_type') == unreal.AdditiveAnimationType.AAT_NONE
    model = anim.get_editor_property('data_model_interface')
    data_controller = unreal.AnimSequencerController()
    data_controller.set_model(model)
    data_controller.open_bracket('Preserve Steel folded shield', False)
    count = 1  # A single key is a constant local accessory transform.
    for bone in accessories:
        transform = ext.get_bone_pose(accessory_pose, bone, unreal.AnimPoseSpaces.LOCAL)
        assert data_controller.set_bone_track_keys(bone, [transform.translation]*count, [transform.rotation]*count, [transform.scale3d]*count, False)
    data_controller.close_bracket(False)
    save(anim)

factory = unreal.BlendSpaceFactory1D()
factory.target_skeleton = target.skeleton
factory.preview_skeletal_mesh = target
blend = create('BS_Steel_NPC_Locomotion', ROOT+'/NPC/Animations', unreal.BlendSpace1D, factory)
params = list(blend.get_editor_property('blend_parameters'))
params[0].set_editor_property('display_name', 'Speed')
params[0].set_editor_property('min', 0.)
params[0].set_editor_property('max', 360.)
params[0].set_editor_property('grid_num', 6)
blend.set_editor_property('blend_parameters', params)
samples = []
for anim, speed, rate in [(animations['idle'], 0., 1.), (animations['run'], 180., .5), (animations['run'], 360., 1.)]:
    sample = unreal.BlendSample()
    sample.set_editor_property('animation', anim)
    sample.set_editor_property('sample_value', unreal.Vector(speed,0,0))
    sample.set_editor_property('rate_scale', rate)
    samples.append(sample)
blend.set_editor_property('sample_data', samples)
blend.set_editor_property('target_weight_interpolation_speed_per_sec', 6.)
factory = unreal.BlueprintFactory()
factory.set_editor_property('parent_class', unreal.WhisperFollower)
bp = create('BP_Steel_NPC', ROOT, unreal.Blueprint, factory)
defaults = unreal.get_default_object(bp.generated_class())
defaults.mesh.set_skeletal_mesh_asset(target)
defaults.mesh.set_editor_property('override_materials', [])
defaults.mesh.set_editor_property('relative_location', unreal.Vector(0,0,-130))
defaults.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0,yaw=-90,roll=0))
defaults.mesh.set_editor_property('relative_scale3d', unreal.Vector(1,1,1))
defaults.capsule_component.set_capsule_size(65.,130.)
defaults.character_movement.set_editor_property('max_walk_speed', 360.)
for key,value in {'max_health':150., 'locomotion_blend_space':blend, 'contact_attack_animation':animations['attack'],
                  'contact_attack_play_rate':1., 'death_animation':animations['death'], 'hit_reaction_animation':None,
                  'contact_interaction_radius':140., 'contact_energy_drain_per_second':20., 'follow_repath_interval':.2}.items():
    defaults.set_editor_property(key,value)
defaults.mesh.set_editor_property('animation_mode',unreal.AnimationMode.ANIMATION_SINGLE_NODE)
data=defaults.mesh.get_editor_property('animation_data')
data.set_editor_property('anim_to_play',blend)
data.set_editor_property('saved_looping',True)
data.set_editor_property('saved_playing',True)
defaults.mesh.set_editor_property('animation_data',data)
defaults.rebuild_locomotion_blend_space()
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
save(blend)
save(bp)
report['npcs'].append({'blueprint':bp.get_path_name(), 'mesh':target.get_path_name(), 'base':True,
    'spec':{'name':'Steel','pack':'ParagonSteel','half':130.}, 'blend':blend.get_path_name(),
    'animations':{k:v.get_path_name() for k,v in animations.items()}})
inspection=json.loads((OUT/'Inspection.json').read_text())
assert not inspection['placed'], 'Placed Steel actors need reconciliation.'
report['placed_source_instances']=inspection['placed']
(OUT/'NPCs.json').write_text(json.dumps(report,indent=2))
unreal.log('STEEL_NPC_SETUP_OK')
