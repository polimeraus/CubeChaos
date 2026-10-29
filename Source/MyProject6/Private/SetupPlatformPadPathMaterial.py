"""Create the dedicated analytic floor-pattern material without changing Lobby assets."""
from pathlib import Path
import unreal

ROOT = '/Game/GameplayActors/Platforms/ParametricPlatform/Materials'
NAME = 'M_PlatformCirclesAndPaths'
PATH = ROOT + '/' + NAME
assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
m = unreal.load_asset(PATH) if unreal.EditorAssetLibrary.does_asset_exist(PATH) else None
if not m:
    m = assets.create_asset(NAME, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    assert m
    def scalar(name, value):
        n = edit.create_material_expression(m, unreal.MaterialExpressionScalarParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', value)
        return n
    def vector(name, value):
        n = edit.create_material_expression(m, unreal.MaterialExpressionVectorParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', unreal.LinearColor(*value))
        return n
    def op(kind, a, b, out_a='', out_b=''):
        n = edit.create_material_expression(m, kind)
        assert edit.connect_material_expressions(a, out_a, n, 'A')
        assert edit.connect_material_expressions(b, out_b, n, 'B')
        return n
    connections = [(edit.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate), '', 'UV'),
                   (scalar('UVWorldUnits', 100), '', 'UVWorldUnits'),
                   (scalar('PadCount', 0), '', 'PadCount'), (scalar('SegmentCount', 0), '', 'SegmentCount'),
                   (scalar('ShowLayout', 1), '', 'ShowLayout')]
    for i in range(16):
        connections.append((vector(f'Pad{i}', (0,0,0,0)), 'RGBA', f'Pad{i}'))
    for i in range(32):
        connections.append((vector(f'Segment{i}', (0,0,0,0)), 'RGBA', f'Segment{i}'))
        connections.append((scalar(f'PathWidth{i}', 0), '', f'PathWidth{i}'))
    inputs = []
    for _, _, name in connections:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    code = (Path(__file__).parent / 'PlatformPadPathMask.ush').read_text()
    for token, prefix, count in [('PAD_ARRAY','Pad',16), ('SEGMENT_ARRAY','Segment',32), ('WIDTH_ARRAY','PathWidth',32)]:
        code = code.replace(token, ','.join(prefix + str(i) for i in range(count)))
    mask = edit.create_material_expression(m, unreal.MaterialExpressionCustom)
    mask.set_editor_property('description', 'Analytic circles, hollow rings and rounded connections')
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mask.set_editor_property('inputs', inputs)
    mask.set_editor_property('code', code)
    for node, output, name in connections:
        assert edit.connect_material_expressions(node, output, mask, name)
    color = vector('Color', (.105,.003,.46,1))
    yellow = vector('YellowColor', (1,.87,.006,1))
    def blend(a, b, out_a='', out_b=''):
        n = op(unreal.MaterialExpressionLinearInterpolate, a, b, out_a, out_b)
        assert edit.connect_material_expressions(mask, '', n, 'Alpha')
        return n
    base = blend(color, yellow, 'RGB', 'RGB')
    emission = blend(op(unreal.MaterialExpressionMultiply, color, scalar('Emission',.18), 'RGB'),
                     op(unreal.MaterialExpressionMultiply, yellow, scalar('YellowEmission',.55), 'RGB'))
    roughness = blend(scalar('Roughness', .19), scalar('PaintRoughness', .26))
    metallic = blend(scalar('Metallic', .32), scalar('PaintMetallic', .05))
    for node, prop in [(base, unreal.MaterialProperty.MP_BASE_COLOR), (emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                       (roughness, unreal.MaterialProperty.MP_ROUGHNESS), (metallic, unreal.MaterialProperty.MP_METALLIC)]:
        assert edit.connect_material_property(node, '', prop)
    edit.layout_material_expressions(m)
    edit.recompile_material(m)
    assert unreal.EditorAssetLibrary.save_loaded_asset(m, False)
unreal.log('PLATFORM_PAD_PATH_MATERIAL_READY ' + PATH)
