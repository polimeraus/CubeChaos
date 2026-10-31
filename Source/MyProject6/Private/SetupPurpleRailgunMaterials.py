"""Keep the rifle readable in dark levels and its purple rings luminous at every angle."""
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
base = '/Game/Weapons/PurpleRailgun'
backup = root / 'Saved/PurpleRailgun/BrightnessBefore'
backup.mkdir(parents=True, exist_ok=True)
edit = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()

def material(name):
    file = root / 'Content/Weapons/PurpleRailgun/Materials' / (name + '.uasset')
    if file.exists() and not (backup / file.name).exists():
        shutil.copy2(file, backup / file.name)
    path = base + '/Materials/' + name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(
        name, base + '/Materials', unreal.Material, unreal.MaterialFactoryNew())

def node(mat, cls, parameter=None, description=None):
    for expression in edit.get_material_expressions(mat):
        if not isinstance(expression, cls): continue
        if parameter is not None and str(expression.get_editor_property('parameter_name')) != parameter: continue
        if description is not None and expression.get_editor_property('description') != description: continue
        return expression
    expression = edit.create_material_expression(mat, cls)
    if parameter: expression.set_editor_property('parameter_name', parameter)
    if description: expression.set_editor_property('description', description)
    return expression

def scalar(mat, name, value):
    expression = node(mat, unreal.MaterialExpressionScalarParameter, parameter=name)
    expression.set_editor_property('default_value', value)
    return expression

def custom(mat, description, code, inputs, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    expression = node(mat, unreal.MaterialExpressionCustom, description=description)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput(); pin.set_editor_property('input_name', name); pins.append(pin)
    expression.set_editor_property('inputs', pins)
    expression.set_editor_property('output_type', output)
    expression.set_editor_property('code', code)
    for name, (source, source_pin) in inputs.items():
        assert edit.connect_material_expressions(source, source_pin, expression, name)
    return expression

body = material('M_PurpleRailgunBody')
samples = {}
for name, sampler in [('BaseColor', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                      ('Normal', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                      ('MetalRough', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)]:
    sample = node(body, unreal.MaterialExpressionTextureSampleParameter2D, parameter=name)
    sample.set_editor_property('texture', unreal.load_asset(base + '/Textures/T_PurpleRailgun_' + name))
    sample.set_editor_property('sampler_type', sampler); samples[name] = sample
lift = node(body, unreal.MaterialExpressionVectorParameter, parameter='GunmetalLift')
lift.set_editor_property('default_value', unreal.LinearColor(.10, .13, .18, 1))
surface = custom(body, 'Readable brushed gunmetal',
    'return saturate(pow(max(Surface.rgb, 0.0), 0.65) * 1.6 + Lift.rgb);',
    {'Surface': (samples['BaseColor'], 'RGB'), 'Lift': (lift, 'RGB')})
metal = custom(body, 'Balanced metal reflectance', 'return Mask * Amount;',
    {'Mask': (samples['MetalRough'], 'B'), 'Amount': (scalar(body, 'MetallicAmount', .4), '')},
    unreal.CustomMaterialOutputType.CMOT_FLOAT1)
rough = custom(body, 'Brushed surface roughness', 'return clamp(Mask, 0.30, 0.55);',
    {'Mask': (samples['MetalRough'], 'G')}, unreal.CustomMaterialOutputType.CMOT_FLOAT1)
fill = custom(body, 'Subtle shadow readability',
    'return Surface.rgb * Fill * (0.45 + 0.55 * saturate(dot(normalize(Normal), normalize(View))));',
    {'Surface': (surface, ''), 'Fill': (scalar(body, 'ShadowFill', .55), ''),
     'Normal': (node(body, unreal.MaterialExpressionPixelNormalWS), ''),
     'View': (node(body, unreal.MaterialExpressionCameraVectorWS), '')})
for expression, prop in [(surface, unreal.MaterialProperty.MP_BASE_COLOR),
                         (samples['Normal'], unreal.MaterialProperty.MP_NORMAL),
                         (metal, unreal.MaterialProperty.MP_METALLIC),
                         (rough, unreal.MaterialProperty.MP_ROUGHNESS),
                         (fill, unreal.MaterialProperty.MP_EMISSIVE_COLOR)]:
    assert edit.connect_material_property(expression, '', prop)

energy = material('M_PurpleRailgunEnergy')
tint = node(energy, unreal.MaterialExpressionVectorParameter, parameter='Tint')
tint.set_editor_property('default_value', unreal.LinearColor(.8, .015, 1, 1))
fresnel = node(energy, unreal.MaterialExpressionFresnel)
fresnel.set_editor_property('exponent', 3.)
fresnel.set_editor_property('base_reflect_fraction', .025)
# Emission remains driven by the weapon's charge animation. The new floor keeps
# front-facing rings bright; rim intensity still increases at grazing angles.
glow = custom(energy, 'Purple glow with a visible front surface',
    'return Tint.rgb * Emission * GlowGain * lerp(GlowFloor, 1.0, Edge);',
    {'Tint': (tint, 'RGB'), 'Emission': (scalar(energy, 'Emission', 4.8), ''),
     'Edge': (fresnel, ''), 'GlowGain': (scalar(energy, 'GlowGain', 3.), ''),
     'GlowFloor': (scalar(energy, 'GlowFloor', .28), '')})
assert edit.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
metal_color = node(energy, unreal.MaterialExpressionVectorParameter, parameter='MetalColor')
metal_color.set_editor_property('default_value', unreal.LinearColor(.16, .035, .23, 1))
assert edit.connect_material_property(metal_color, '', unreal.MaterialProperty.MP_BASE_COLOR)
for name, value, prop in [('Metallic', .45, unreal.MaterialProperty.MP_METALLIC),
                          ('Roughness', .25, unreal.MaterialProperty.MP_ROUGHNESS)]:
    assert edit.connect_material_property(scalar(energy, name, value), '', prop)

for mat in [body, energy]:
    edit.layout_material_expressions(mat)
    edit.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
unreal.log('PURPLE_RAILGUN_BRIGHT_MATERIALS_SAVED')
