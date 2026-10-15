"""Create the analytic material and reusable pickup Blueprint. Does not edit levels.

UnrealEditor-Cmd MyProject6.uproject -run=pythonscript -script=<this file>
Run Tests/VerifyPickupRendering.py afterwards to validate the compiled shader.
"""
from datetime import datetime
from pathlib import Path
import shutil
import unreal

folder = '/Game/GameplayActors/Pickups/ParametricPickup'
name = 'M_PickupSurface'
material_folder = '/Game/GameplayActors/Pickups/Shared/Materials'
path = material_folder + '/' + name
edit = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
if material:
    source = Path(unreal.Paths.project_content_dir()) / 'GameplayActors/Pickups/Shared/Materials/M_PickupSurface.uasset'
    if source.exists():
        backup = Path(unreal.Paths.project_saved_dir()) / 'Pickups/Backups' / datetime.now().strftime('%Y%m%d_%H%M%S')
        backup.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, backup / source.name)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, material_folder, unreal.Material, unreal.MaterialFactoryNew())
assert material, 'Could not create pickup material'
edit.delete_all_material_expressions(material)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
material.set_editor_property('two_sided', False)

def expression(cls):
    return edit.create_material_expression(material, cls)

def scalar(name, default, group):
    node = expression(unreal.MaterialExpressionScalarParameter)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', default)
    node.set_editor_property('group', group)
    return node

def vector(name, color):
    node = expression(unreal.MaterialExpressionVectorParameter)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', unreal.LinearColor(*color, 1))
    return node

nodes = {'UV': expression(unreal.MaterialExpressionTextureCoordinate), 'Clock': expression(unreal.MaterialExpressionTime)}
nodes['Clock'].set_editor_property('ignore_pause', False)
for key, value, group in [
    ('Motif', 0, 'Icon'), ('IconScale', .72, 'Icon'), ('IconRotation', 0, 'Icon'),
    ('PulseEnabled', 1, 'Glow'), ('PulsePeriod', 2, 'Glow'), ('PulsePhase', 0, 'Glow'),
    ('MinimumGlow', 0, 'Glow'), ('MaximumGlow', 6, 'Glow')]:
    nodes[key] = scalar(key, value, group)
shader = expression(unreal.MaterialExpressionCustom)
shader.set_editor_property('description', 'Procedural pickup icons + timed glow')
inputs = []
for key in nodes:
    pin = unreal.CustomInput()
    pin.set_editor_property('input_name', key)
    inputs.append(pin)
shader.set_editor_property('inputs', inputs)
shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT2)
shader.set_editor_property('code', (Path(__file__).with_name('PickupSurfaceMaterial.ush')).read_text())
for key, node in nodes.items():
    assert edit.connect_material_expressions(node, '', shader, key), key

def mask(channel):
    node = expression(unreal.MaterialExpressionComponentMask)
    for key in 'rgba':
        node.set_editor_property(key, key == channel)
    assert edit.connect_material_expressions(shader, '', node, '')
    return node

icon = vector('IconColor', (.03, .75, 1))
body = vector('BodyColor', (.025, .04, .065))
base = expression(unreal.MaterialExpressionLinearInterpolate)
assert edit.connect_material_expressions(body, 'RGB', base, 'A')
assert edit.connect_material_expressions(icon, 'RGB', base, 'B')
assert edit.connect_material_expressions(mask('r'), '', base, 'Alpha')
assert edit.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
emission = expression(unreal.MaterialExpressionMultiply)
assert edit.connect_material_expressions(icon, 'RGB', emission, 'A')
assert edit.connect_material_expressions(mask('g'), '', emission, 'B')
assert edit.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
assert edit.connect_material_property(scalar('Roughness', .32, 'Body'), '', unreal.MaterialProperty.MP_ROUGHNESS)
assert edit.connect_material_property(scalar('Metallic', .3, 'Body'), '', unreal.MaterialProperty.MP_METALLIC)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert assets.save_loaded_asset(material, False)

bp_path = folder + '/BP_ParametricPickup'
bp = unreal.load_asset(bp_path) if assets.does_asset_exist(bp_path) else None
if not bp:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ParametricPickup)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_ParametricPickup', folder, unreal.Blueprint, factory)
assert bp, 'Could not create pickup Blueprint'
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.get_default_object(bp.generated_class()).set_editor_property('surface_material', material)
assert assets.save_loaded_asset(bp, False)
unreal.log('PARAMETRIC_PICKUP_READY ' + bp_path)
