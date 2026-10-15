"""Create persistent Paragon NPC classes after Scripts/move_paragon_content.ps1.

Run via UnrealEditor-Cmd -run=pythonscript with PythonScriptPlugin and
EditorScriptingUtilities. Vendor assets remain intact; NPC gameplay and animation
are authored in new Blueprint defaults. No gameplay level placement is added.
"""
import json
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.project_saved_dir()) / 'CharacterMigration'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SPECS = [
    dict(pack='ParagonCrunch', hero='Crunch', name='Crunch', mesh='Crunch', idle='Idle_Combat',
         run='Jog_Fwd', attack='Ability_GutPunch', death='Death_A', half=130., radius=65., speed=360.),
    dict(pack='ParagonFengMao', hero='FengMao', name='FengMao', mesh='FengMao_GDC', idle='Idle',
         run='Run_Fwd', attack='Melee_A_Med_InPlace', death='Death_Fwd', half=110., radius=50., speed=450.),
    dict(pack='ParagonGRIMexe', hero='GRIM', name='GRIMexe', mesh='GRIM_GDC', idle='Idle',
         run='Jog_Fwd_InPlace', attack='Primary_Fire_Med_A', death='Death_Fwd', half=130., radius=65., speed=360.),
]


def require(path):
    obj = unreal.load_asset(path)
    assert obj, path
    return obj


def create(name, folder, cls, factory):
    path = folder + '/' + name
    return require(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, folder, cls, factory)


def save(obj):
    assert LIB.save_loaded_asset(obj, False), obj.get_path_name()


def presentation(defaults, mesh, spec):
    defaults.mesh.set_skeletal_mesh_asset(mesh)
    defaults.mesh.set_editor_property('override_materials', [])
    defaults.mesh.set_editor_property('relative_location', unreal.Vector(0, 0, -spec['half']))
    defaults.mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=-90, roll=0))
    defaults.mesh.set_editor_property('relative_scale3d', unreal.Vector(1, 1, 1))
    defaults.capsule_component.set_capsule_size(spec['radius'], spec['half'])


audit = json.loads((OUT / 'Audit.json').read_text())
report = {'npcs': [], 'level_modified': False, 'placed_source_instances': [], 'skeletal_poses': {}}
for spec in SPECS:
    root = '/Game/NPC/' + spec['pack']
    source = root + '/Characters/Heroes/' + spec['hero']
    mesh = require(source + '/Meshes/' + spec['mesh'])
    skeleton = mesh.skeleton
    animations = {key: require(source + '/Animations/' + spec[key]) for key in ('idle', 'run', 'attack', 'death')}
    for anim in animations.values():
        assert anim.get_editor_property('skeleton') == skeleton
        assert anim.get_editor_property('additive_anim_type') == unreal.AdditiveAnimationType.AAT_NONE
    factory = unreal.BlendSpaceFactory1D()
    factory.target_skeleton = skeleton
    factory.preview_skeletal_mesh = mesh
    blend = create('BS_' + spec['name'] + '_NPC_Locomotion', root + '/NPC/Animations', unreal.BlendSpace1D, factory)
    parameters = list(blend.get_editor_property('blend_parameters'))
    parameters[0].set_editor_property('display_name', 'Speed')
    parameters[0].set_editor_property('min', 0.)
    parameters[0].set_editor_property('max', spec['speed'])
    parameters[0].set_editor_property('grid_num', 6)
    blend.set_editor_property('blend_parameters', parameters)
    samples = []
    for anim, speed, rate in [(animations['idle'], 0., 1.),
                              (animations['run'], spec['speed'] * .5, .5),
                              (animations['run'], spec['speed'], 1.)]:
        sample = unreal.BlendSample()
        sample.set_editor_property('animation', anim)
        sample.set_editor_property('sample_value', unreal.Vector(speed, 0, 0))
        sample.set_editor_property('rate_scale', rate)
        samples.append(sample)
    blend.set_editor_property('sample_data', samples)
    blend.set_editor_property('target_weight_interpolation_speed_per_sec', 6.)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.WhisperFollower)
    bp = create('BP_' + spec['name'] + '_NPC', root, unreal.Blueprint, factory)
    defaults = unreal.get_default_object(bp.generated_class())
    presentation(defaults, mesh, spec)
    defaults.character_movement.set_editor_property('max_walk_speed', spec['speed'])
    defaults.set_editor_property('max_health', 150.)
    defaults.set_editor_property('locomotion_blend_space', blend)
    defaults.set_editor_property('contact_attack_animation', animations['attack'])
    defaults.set_editor_property('contact_attack_play_rate', 1.)
    defaults.set_editor_property('death_animation', animations['death'])
    # Vendor hit reactions are additive; the project's skeleton-independent bend
    # component handles impacts without playing an additive clip as a full pose.
    defaults.set_editor_property('hit_reaction_animation', None)
    defaults.set_editor_property('contact_interaction_radius', 140.)
    defaults.set_editor_property('contact_energy_drain_per_second', 20.)
    defaults.set_editor_property('follow_repath_interval', .2)
    defaults.mesh.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    data = defaults.mesh.get_editor_property('animation_data')
    data.set_editor_property('anim_to_play', blend)
    data.set_editor_property('saved_looping', True)
    data.set_editor_property('saved_playing', True)
    defaults.mesh.set_editor_property('animation_data', data)
    defaults.rebuild_locomotion_blend_space()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save(blend)
    save(bp)
    entry = {'blueprint': bp.get_path_name(), 'mesh': mesh.get_path_name(), 'base': True, 'spec': spec,
             'blend': blend.get_path_name(), 'animations': {k: v.get_path_name() for k,v in animations.items()}}
    report['npcs'].append(entry)
    # Preserve the original scale and geometry of every supplied skin, while
    # inheriting behavior from the hero's NPC Blueprint.
    variants = [a['package'].replace('/Game/', '/Game/NPC/', 1) for a in audit['assets']
                if a['class'] == 'SkeletalMesh' and a['package'].startswith('/Game/' + spec['pack'] + '/') and '/Skins/' in a['package']]
    for path in variants:
        variant_mesh = require(path)
        assert variant_mesh.skeleton == skeleton
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', bp.generated_class())
        variant = create('BP_' + variant_mesh.get_name() + '_NPC', root + '/Variants', unreal.Blueprint, factory)
        child = unreal.get_default_object(variant.generated_class())
        presentation(child, variant_mesh, spec)
        unreal.BlueprintEditorLibrary.compile_blueprint(variant)
        save(variant)
        report['npcs'].append(dict(entry, blueprint=variant.get_path_name(), mesh=variant_mesh.get_path_name(), base=False))
    ext = unreal.AnimPoseExtensions
    pose = ext.get_anim_pose_at_time(animations['idle'], .5, unreal.AnimPoseEvaluationOptions())
    report['skeletal_poses'][spec['name']] = {str(b): str(ext.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD).translation)
        for b in ext.get_bone_names(pose) if any(t in str(b).lower() for t in ('foot', 'head', 'root', 'ball'))}
    (OUT / 'NPCs.json').write_text(json.dumps(report, indent=2))
    unreal.log('PARAGON_NPC_READY ' + bp.get_path_name())

# Verify no placed source model is being left with player-only behavior. The
# preflight found none in the gameplay map, so level-specific data stays local.
source_details = json.loads((OUT / 'SourceDetails.json').read_text())
report['placed_source_instances'] = source_details['placed']
assert not report['placed_source_instances'], 'Placed source actors require explicit reconciliation before completion.'
assert len(report['npcs']) == 15
(OUT / 'NPCs.json').write_text(json.dumps(report, indent=2))
unreal.log('PARAGON_NPC_SETUP_OK: 3 heroes, 12 inherited skin variants')
