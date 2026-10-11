"""Create the playable enemy assets without changing the supplied source or gameplay map.

Run with UnrealEditor-Cmd -run=pythonscript after building MyProject6Editor.
"""
import json
from pathlib import Path
import unreal

ROOT = '/Game/Characters/DarkSorcerer'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
OUT = Path(unreal.Paths.project_saved_dir()) / 'DarkSorcerer'
OUT.mkdir(parents=True, exist_ok=True)
report = {}


def require(path):
    obj = unreal.load_asset(path)
    assert obj, path
    return obj


def duplicate(source, destination):
    return (require(destination) if LIB.does_asset_exist(destination)
            else LIB.duplicate_asset(source, destination))


def save(asset):
    assert LIB.save_loaded_asset(asset, False), asset.get_path_name()


def fade_material(source, destination):
    mat = duplicate(source, destination)
    edit = unreal.MaterialEditingLibrary
    # The generated material is private to this NPC. Reuse the artist's color/emissive graph.
    names = [str(n) for n in edit.get_scalar_parameter_names(mat)]
    if 'NPCDeathOpacity' not in names:
        opacity = edit.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -200, 450)
        opacity.set_editor_property('parameter_name', 'NPCDeathOpacity')
        opacity.set_editor_property('default_value', 1.0)
        assert edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property('dither_opacity_mask', True)
    mat.set_editor_property('used_with_skeletal_mesh', True)
    mat.set_editor_property('used_with_clothing', True)
    edit.recompile_material(mat)
    save(mat)
    return mat


body_mat = fade_material(ROOT + '/Experiments/V3/Materials/M_DarkSorcerer', ROOT + '/Materials/M_DarkSorcerer_NPC')
staff_mat = fade_material(ROOT + '/Materials/M_DarkSorcerer_Staff', ROOT + '/Materials/M_DarkSorcerer_Staff_NPC')
body = duplicate(ROOT + '/Experiments/V3/Meshes/SK_DarkSorcerer', ROOT + '/Meshes/SK_DarkSorcerer_NPC')
slots = list(body.get_editor_property('materials'))
for slot in slots:
    slot.set_editor_property('material_interface', body_mat)
body.set_editor_property('materials', slots)
report['cloth'] = []
for cloth in body.get_editor_property('mesh_clothing_assets'):
    assert cloth.get_path_name().startswith(body.get_path_name() + ':'), 'Cloth must be a private duplicate'
    for key, config in cloth.get_editor_property('cloth_configs').items():
        if config.get_class().get_name() == 'ChaosClothSharedSimConfig':
            config.set_editor_property('IterationCount', 6)
            config.set_editor_property('MaxIterationCount', 8)
            config.set_editor_property('SubdivisionCount', 1)
            report['cloth'].append({'iterations': 6, 'max_iterations': 8, 'subdivisions': 1})
save(body)
staff = duplicate(ROOT + '/Meshes/SM_DarkSorcerer_Staff', ROOT + '/Meshes/SM_DarkSorcerer_Staff_NPC')
staff.set_material(0, staff_mat)
save(staff)

# Reuse the established movement graph, with a private walk-only blend space.
blend = duplicate('/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run', ROOT + '/Animations/BS_DarkSorcerer_Locomotion')
reference = unreal.AnimPoseExtensions.get_reference_pose(body.get_editor_property('skeleton'))
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property('should_retarget', False)
grip = unreal.AnimPoseExtensions.get_anim_pose_at_time(require('/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03'), .2, options)
planted_pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(require('/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle'), 0, options)
holding = {}


def holding_animation(source, planted=False):
    name = source.get_name()
    if name in holding:
        return holding[name]
    target = duplicate(source.get_path_name(), ROOT + '/Animations/' + ('A_DS_Cast' if planted else 'A_DS_' + name))
    controller = target.controller
    tracks = {str(n) for n in unreal.AnimationLibrary.get_animation_track_names(target)}
    controller.open_bracket('Bake staff carrying arm and closed grip', False)
    try:
        for bone in unreal.AnimPoseExtensions.get_bone_names(reference):
            name_bone = str(bone)
            arm = name_bone.endswith('_r') and name_bone.startswith(('clavicle', 'upperarm', 'lowerarm', 'hand'))
            finger = name_bone.endswith('_r') and name_bone.startswith(('thumb', 'index', 'middle', 'ring', 'pinky'))
            leg = planted and name_bone.startswith(('root', 'pelvis', 'thigh', 'calf', 'foot', 'ball', 'ik_foot'))
            if arm or finger or leg:
                if name_bone not in tracks:
                    controller.add_bone_curve(bone, False)
                source_pose = planted_pose if leg else (grip if finger else reference)
                pose = unreal.AnimPoseExtensions.get_bone_pose(source_pose, bone, unreal.AnimPoseSpaces.LOCAL)
                assert controller.set_bone_track_keys(bone, [pose.translation], [pose.rotation], [pose.scale3d], False), name_bone
    finally:
        controller.close_bracket(False)
    save(target)
    holding[name] = target
    return target


report['original_samples'] = []
samples = list(require('/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run').get_editor_property('sample_data'))
for sample in samples:
    anim = sample.get_editor_property('animation')
    point = sample.get_editor_property('sample_value')
    report['original_samples'].append({'animation': anim.get_path_name() if anim else None, 'point': str(point)})
    if anim and 'Jog' in anim.get_path_name():
        path = anim.get_path_name().split('.')[0].replace('/Jog/', '/Walk/').replace('_Jog_', '_Walk_')
        anim = require(path)
        sample.set_editor_property('rate_scale', 1.5)
    if anim:
        sample.set_editor_property('animation', holding_animation(anim))
blend.set_editor_property('sample_data', samples)
abp = duplicate('/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed', ROOT + '/Animations/ABP_DarkSorcerer')
count = unreal.DarkSorcererNPC.configure_locomotion(abp, blend)
assert count > 0, 'Locomotion blend space was not connected'
unreal.BlueprintEditorLibrary.compile_blueprint(abp)
save(blend)
save(abp)
report['locomotion_nodes'] = count

path = ROOT + '/BP_DarkSorcerer_NPC'
if LIB.does_asset_exist(path):
    bp = require(path)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.DarkSorcererNPC)
    bp = TOOLS.create_asset('BP_DarkSorcerer_NPC', ROOT, unreal.Blueprint, factory)
defaults = unreal.get_default_object(bp.generated_class())
defaults.mesh.set_skeletal_mesh_asset(body)
defaults.mesh.set_anim_instance_class(abp.generated_class())
defaults.get_editor_property('staff').set_static_mesh(staff)
defaults.set_editor_property('contact_attack_animation', holding_animation(require('/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03'), True))
defaults.set_editor_property('death_fade_materials', [body_mat, body_mat])
defaults.set_editor_property('contact_attack_play_rate', 0.8)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
save(bp)
report['blueprint'] = path
report['body'] = body.get_path_name()
report['staff'] = staff.get_path_name()
report['skeleton'] = body.get_editor_property('skeleton').get_path_name()
report['cloth_warning'] = 'Cloth simulation still has CPU cost; benchmark intended crowd size. Fast jog replaced with walking.'
report['map_modified'] = False
(OUT / 'Setup.json').write_text(json.dumps(report, indent=2))
unreal.log('DARK_SORCERER_SETUP_OK ' + json.dumps(report))
