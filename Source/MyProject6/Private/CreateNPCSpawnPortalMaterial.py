"""Build the small procedural spawner hologram. No levels or NPC assets are edited.

UnrealEditor-Cmd MyProject6.uproject -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi
"""
from datetime import datetime
from pathlib import Path
import shutil
import unreal

folder = '/Game/NPC/Spawning'
name = 'M_NPCSpawnPortal'
path = f'{folder}/{name}'
source = Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private/NPCSpawnPortalMaterial.ush'
material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material:
    package_file = Path(unreal.Paths.project_content_dir()) / 'NPC/Spawning/M_NPCSpawnPortal.uasset'
    if package_file.exists():
        backup = Path(unreal.Paths.project_saved_dir()) / 'NPCSpawnPoint/MaterialBackups' / datetime.now().strftime('%Y%m%d_%H%M%S')
        backup.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package_file, backup / package_file.name)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError('Could not create ' + path)

edit = unreal.MaterialEditingLibrary
# Native defaults load this material during startup, so its existing expressions can be
# rooted. Reuse them: deleting rooted expressions asserts in UE's material editing API.
remaining = list(edit.get_material_expressions(material))


def expression(node_class):
    for node in remaining:
        if isinstance(node, node_class):
            remaining.remove(node)
            return node
    return edit.create_material_expression(material, node_class)


material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('two_sided', True)
# Also expands the renderer bounds for the upward GPU animation.
material.set_editor_property('max_world_position_offset_displacement', 600.0)

nodes = {}
uv = expression(unreal.MaterialExpressionTextureCoordinate)
nodes['UV'] = (uv, '')
clock = expression(unreal.MaterialExpressionTime)
clock.set_editor_property('ignore_pause', False)
nodes['Clock'] = (clock, '')
color = expression(unreal.MaterialExpressionVectorParameter)
color.set_editor_property('parameter_name', 'Tint')
color.set_editor_property('default_value', unreal.LinearColor(0.02, 0.75, 1.0, 1.0))
nodes['Tint'] = (color, 'RGB')
for parameter, value in {
    'AnimationSpeed': 1.0, 'Phase': 0.0, 'Layer': 0.0,
    'Brightness': 3.0, 'RiseHeight': 170.0,
    'SpawnTime': -100000.0, 'PulseDuration': 0.65, 'PulseStrength': 2.0,
}.items():
    node = expression(unreal.MaterialExpressionScalarParameter)
    node.set_editor_property('parameter_name', parameter)
    node.set_editor_property('default_value', value)
    nodes[parameter] = (node, '')


def custom(code, names, output_type):
    node = expression(unreal.MaterialExpressionCustom)
    inputs = []
    for key in names:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', key)
        inputs.append(item)
    node.set_editor_property('inputs', inputs)
    node.set_editor_property('output_type', output_type)
    node.set_editor_property('code', code)
    for key in names:
        upstream, output = nodes[key]
        if not edit.connect_material_expressions(upstream, output, node, key):
            raise RuntimeError('Cannot connect ' + key)
    return node


rgba = custom(source.read_text(), [key for key in nodes if key != 'RiseHeight'], unreal.CustomMaterialOutputType.CMOT_FLOAT4)
rgb = expression(unreal.MaterialExpressionComponentMask)
alpha = expression(unreal.MaterialExpressionComponentMask)
for mask, channels in [(rgb, 'rgb'), (alpha, 'a')]:
    for channel in 'rgba':
        mask.set_editor_property(channel, channel in channels)
    if not edit.connect_material_expressions(rgba, '', mask, ''):
        raise RuntimeError('Cannot connect portal RGBA to component mask ' + channels)
if not edit.connect_material_property(rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('Cannot connect portal emissive color')
if not edit.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError('Cannot connect portal opacity')
wpo = custom('return float3(0, 0, Layer * frac(Clock * AnimationSpeed * 0.38 + Phase) * RiseHeight);',
             ['Layer', 'Clock', 'AnimationSpeed', 'Phase', 'RiseHeight'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
if not edit.connect_material_property(wpo, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
    raise RuntimeError('Cannot connect portal rise animation')
edit.layout_material_expressions(material)
errors = edit.recompile_material(material)
if errors:
    raise RuntimeError('Portal material errors: ' + str(errors))
if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
    raise RuntimeError('Could not save ' + path)
unreal.log('NPC_SPAWN_PORTAL_MATERIAL_READY ' + path)
