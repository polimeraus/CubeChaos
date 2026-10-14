"""Create the soft energy-trail material; safe to run again."""
from pathlib import Path
import unreal

root = '/Game/Weapons/Railgun'
path = root + '/M_RailTrail'
edit = unreal.MaterialEditingLibrary
material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_RailTrail', root, unreal.Material, unreal.MaterialFactoryNew())
# Update existing expressions in place: constructor-loaded material nodes can be
# rooted by Unreal, so deleting them during a later setup pass is unsafe.
expressions = list(edit.get_material_expressions(material))
def node_for(cls, parameter=None, channels=None):
    for expression in expressions:
        if not isinstance(expression, cls):
            continue
        if parameter is not None and str(expression.get_editor_property('parameter_name')) != parameter:
            continue
        if channels is not None and any(expression.get_editor_property(ch) != (ch in channels) for ch in 'rgba'):
            continue
        return expression
    expression = edit.create_material_expression(material, cls)
    expressions.append(expression)
    return expression

material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
material.set_editor_property('two_sided', True)
uv = node_for(unreal.MaterialExpressionTextureCoordinate)
tint = node_for(unreal.MaterialExpressionVectorParameter, 'Tint')
tint.set_editor_property('parameter_name', 'Tint')
tint.set_editor_property('default_value', unreal.LinearColor(.02, .55, 1, 1))
nodes = {'UV':(uv, ''), 'Tint':(tint, 'RGB')}
for name, value in {'Brightness':5.0, 'Density':1.2, 'BeamLength':1000.0, 'Age':0.0, 'Fade':1.0}.items():
    node = node_for(unreal.MaterialExpressionScalarParameter, name)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    nodes[name] = (node, '')
shader = node_for(unreal.MaterialExpressionCustom)
pins = []
for name in nodes:
    pin = unreal.CustomInput()
    pin.set_editor_property('input_name', name)
    pins.append(pin)
shader.set_editor_property('inputs', pins)
shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
shader.set_editor_property('code', (Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private/RailgunTrail.ush').read_text())
for name, (node, output) in nodes.items():
    assert edit.connect_material_expressions(node, output, shader, name)
for channels, prop in [('rgb', unreal.MaterialProperty.MP_EMISSIVE_COLOR), ('a', unreal.MaterialProperty.MP_OPACITY)]:
    mask = node_for(unreal.MaterialExpressionComponentMask, channels=channels)
    for ch in 'rgba':
        mask.set_editor_property(ch, ch in channels)
    assert edit.connect_material_expressions(shader, '', mask, '')
    assert edit.connect_material_property(mask, '', prop)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('RAILGUN_SOFT_TRAIL_READY')
