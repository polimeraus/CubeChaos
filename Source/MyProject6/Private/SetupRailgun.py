"""Create the railgun's emissive material and original synthesized firing sound."""
import json
import runpy
import math
import random
import struct
import wave
from pathlib import Path
import unreal

root = '/Game/Weapons/Railgun'
edit = unreal.MaterialEditingLibrary
path = root + '/M_RailEnergy'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_RailEnergy', root, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('two_sided', True)
    tint = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(.04, .6, 1, 1))
    brightness = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    brightness.set_editor_property('parameter_name', 'Brightness')
    brightness.set_editor_property('default_value', 6.0)
    multiply = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
    edit.connect_material_expressions(tint, 'RGB', multiply, 'A')
    edit.connect_material_expressions(brightness, '', multiply, 'B')
    edit.connect_material_property(multiply, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 1.0)
    edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)

output = Path(unreal.Paths.project_saved_dir()) / 'Railgun'
output.mkdir(parents=True, exist_ok=True)
arms_path = root + '/M_RailArms'
if not unreal.EditorAssetLibrary.does_asset_exist(arms_path):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_RailArms', root, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('used_with_skeletal_mesh', True)
    color = edit.create_material_expression(material, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(.11, .16, .20, 1))
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = edit.create_material_expression(material, unreal.MaterialExpressionConstant)
    roughness.set_editor_property('r', .42)
    edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    position = edit.create_material_expression(material, unreal.MaterialExpressionPreSkinnedPosition)
    mask = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
    pin = unreal.CustomInput()
    pin.set_editor_property('input_name', 'Position')
    mask.set_editor_property('inputs', [pin])
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    # Manny's reference-pose arms extend along local X. Clip the torso/head/legs
    # before skinning so camera pitch and the equipped animation cannot expose them.
    mask.set_editor_property('code', 'return (abs(Position.x) > 25.0 && Position.z > 65.0) ? 1.0 : 0.0;')
    interpolator = edit.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator)
    edit.connect_material_expressions(position, '', interpolator, '')
    edit.connect_material_expressions(interpolator, '', mask, 'Position')
    edit.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)

arms_material = unreal.load_asset(arms_path)
nodes = list(edit.get_material_expressions(arms_material))
mask = edit.get_material_property_input_node(arms_material, unreal.MaterialProperty.MP_OPACITY_MASK)
mask_code = 'return (abs(Position.x) > 25.0 && Position.z > 65.0) ? 1.0 : 0.0;'
if mask.get_editor_property('code') != mask_code:
    mask.set_editor_property('code', mask_code)
    edit.recompile_material(arms_material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(arms_material, False)
if not any(isinstance(node, unreal.MaterialExpressionVertexInterpolator) for node in nodes):
    position = next(node for node in nodes if isinstance(node, unreal.MaterialExpressionPreSkinnedPosition))
    mask = edit.get_material_property_input_node(arms_material, unreal.MaterialProperty.MP_OPACITY_MASK)
    interpolator = edit.create_material_expression(arms_material, unreal.MaterialExpressionVertexInterpolator)
    assert edit.connect_material_expressions(position, '', interpolator, '')
    assert edit.connect_material_expressions(interpolator, '', mask, 'Position')
    edit.layout_material_expressions(arms_material)
    edit.recompile_material(arms_material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(arms_material, False)

runpy.run_path(str(Path(__file__).with_name('SetupRailgunArms.py')), run_name='__main__')

if not unreal.EditorAssetLibrary.does_asset_exist(root + '/S_RailFire'):
    sound_file = output / 'S_RailFire.wav'
    rng = random.Random(27)
    rate = 48000
    samples = bytearray()
    phase = 0.0
    for index in range(int(rate * .62)):
        t = index / rate
        phase += 2 * math.pi * (170 + 2200 * math.exp(-t * 24)) / rate
        attack = min(1.0, t / .0015)
        crack = rng.uniform(-1, 1) * math.exp(-t * 68) * .52
        zap = (math.sin(phase) + .25 * math.sin(phase * 2.013)) * math.exp(-t * 9) * .33
        bass = math.sin(2 * math.pi * 77 * t) * math.exp(-t * 15) * .25
        samples.extend(struct.pack('<h', int(max(-1, min(1, (crack + zap + bass) * attack)) * 30000)))
    with wave.open(str(sound_file), 'wb') as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes(samples)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(sound_file))
    task.set_editor_property('destination_path', root)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    assert unreal.load_asset(root + '/S_RailFire')

player_cls = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
player = unreal.get_default_object(player_cls)
report = []
for comp in player.get_components_by_class(unreal.SkeletalMeshComponent):
    report.append({'component': comp.get_name(), 'mesh': str(comp.get_editor_property('skeletal_mesh_asset')),
                   'anim': str(comp.get_editor_property('anim_class')), 'sockets': [str(x) for x in comp.get_all_socket_names()]})
rifle = unreal.load_asset('/Game/Weapons/Rifle/Meshes/SM_Rifle')
report.append({'rifle_bounds': str(rifle.get_bounds())})
(output / 'Assets.json').write_text(json.dumps(report, indent=2))
source_dir = Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private'
runpy.run_path(str(source_dir / 'SetupRailgunTrail.py'), run_name='__main__')
runpy.run_path(str(source_dir / 'SetupRailgunGrip.py'), run_name='__main__')
unreal.log('RAILGUN_ASSETS_READY')
