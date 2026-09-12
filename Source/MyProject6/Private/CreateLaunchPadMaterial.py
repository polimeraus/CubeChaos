"""Create/update the procedural launch-pad material; no textures or level edits."""
from datetime import datetime
from pathlib import Path
import shutil
import unreal

path = '/Game/Gameplay/LaunchPad/M_LaunchPad'
material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material:
    asset_file = Path(unreal.Paths.project_content_dir()) / 'Gameplay/LaunchPad/M_LaunchPad.uasset'
    backup = Path(unreal.Paths.project_saved_dir()) / 'LaunchPad/MaterialBackups' / datetime.now().strftime('%Y%m%d_%H%M%S')
    backup.mkdir(parents=True, exist_ok=True)
    shutil.copy2(asset_file, backup / asset_file.name)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_LaunchPad', '/Game/Gameplay/LaunchPad', unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError('Could not create launch-pad material')
edit = unreal.MaterialEditingLibrary
remaining = list(edit.get_material_expressions(material))
def expression(cls):
    for node in remaining:
        if isinstance(node, cls):
            remaining.remove(node)
            return node
    return edit.create_material_expression(material, cls)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('two_sided', True)
nodes = {'UV': (expression(unreal.MaterialExpressionTextureCoordinate), '')}
clock = expression(unreal.MaterialExpressionTime)
clock.set_editor_property('ignore_pause', False)
nodes['Clock'] = (clock, '')
tint = expression(unreal.MaterialExpressionVectorParameter)
tint.set_editor_property('parameter_name', 'Tint')
tint.set_editor_property('default_value', unreal.LinearColor(.02, .75, 1, 1))
nodes['Tint'] = (tint, 'RGB')
for name, default in {'AnimationSpeed': 1, 'Brightness': 3, 'SpawnTime': -100000}.items():
    node = expression(unreal.MaterialExpressionScalarParameter)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', default)
    nodes[name] = (node, '')
shader = expression(unreal.MaterialExpressionCustom)
inputs = []
for name in nodes:
    pin = unreal.CustomInput()
    pin.set_editor_property('input_name', name)
    inputs.append(pin)
shader.set_editor_property('inputs', inputs)
shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
shader.set_editor_property('code', (Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private/LaunchPadMaterial.ush').read_text())
for name, (node, pin) in nodes.items():
    if not edit.connect_material_expressions(node, pin, shader, name):
        raise RuntimeError('Could not connect ' + name)
mask = expression(unreal.MaterialExpressionComponentMask)
for channel in 'rgba':
    mask.set_editor_property(channel, channel in 'rgb')
assert edit.connect_material_expressions(shader, '', mask, '')
assert edit.connect_material_property(mask, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('LAUNCH_PAD_MATERIAL_READY ' + path)
