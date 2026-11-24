"""Author the two mesh instruments and the blue railgun's enamel finishes."""
import unreal

edit = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary

# UV0 lies in the dial plane. The graduations, brass pivot and bezel are part of the mesh.
DIAL = """
float2 p = (UV - 0.5) * float2(2, -2);
float angle = radians(lerp(28.0, 152.0, saturate(Discharge)));
float2 axis = float2(cos(angle), sin(angle));
float along = dot(p, axis);
float across = abs(dot(p, float2(-axis.y, axis.x)));
float width = lerp(0.077, 0.016, saturate((along + 0.16) / 0.98));
float distanceToNeedle = max(across - width, max(-0.16 - along, along - 0.82));
float aa = max(fwidth(distanceToNeedle), 0.002);
float edge = 1.0 - smoothstep(-aa, aa, distanceToNeedle - 0.012);
float needle = 1.0 - smoothstep(-aa, aa, distanceToNeedle);
float3 face = float3(0.95, 0.78, 0.50);
return lerp(lerp(face, float3(0.19, 0.022, 0.024), edge), float3(0.74, 0.021, 0.033), needle);
"""

METER = """
float charge = 1.0 - saturate(Discharge);
float x = (UV.x - 0.082) / 0.836;
float y = UV.y;
float count = 8.0;
float position = clamp(x * count, 0.0, count - 0.0001);
float cell = floor(position);
float2 inset = float2(abs(frac(position) - 0.5) - 0.407, abs(y - 0.49) - 0.18);
float d = max(max(inset.x / count, inset.y), max(-x, x - 1.0));
float aa = max(fwidth(d), 0.002);
float bars = 1.0 - smoothstep(-aa, aa, d);
float fill = saturate(charge * count - cell);
float3 lit = lerp(float3(0.05, 1.13, 1.55), float3(0.63, 1.50, 1.69), cell / 7.0);
float3 glass = lerp(float3(0.008, 0.024, 0.037), float3(0.019, 0.071, 0.086), saturate(1.0 - y));
float3 color = lerp(glass, lerp(float3(0.025, 0.13, 0.15), lit, fill), bars);
float ticks = step(0.0, x) * step(x, 1.0) * step(0.72, y) * step(y, 0.83)
            * (1.0 - smoothstep(0.10, 0.23, abs(frac(position + 0.5) - 0.5)));
return lerp(color, float3(0.42, 0.55, 0.57), ticks);
"""


def asset(name, folder, kind, factory):
    path = folder + '/' + name
    if library.does_asset_exist(path):
        return unreal.load_asset(path)
    return assets.create_asset(name, folder, kind, factory)


def make_instrument(analog):
    folder = '/Game/Weapons/PurpleRailgun/Materials' if analog else '/Game/Weapons/Railgun/Materials'
    name = 'M_CometChargeDial' if analog else 'M_RailChargeMeter'
    material = asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    expressions = list(edit.get_material_expressions(material))

    def node(cls, label=None):
        for existing in expressions:
            if isinstance(existing, cls) and (label is None or str(existing.get_editor_property('parameter_name')) == label):
                return existing
        item = edit.create_material_expression(material, cls)
        if label:
            item.set_editor_property('parameter_name', label)
        expressions.append(item)
        return item

    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT if analog else unreal.MaterialShadingModel.MSM_UNLIT)
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    discharge = node(unreal.MaterialExpressionScalarParameter, 'RailDischarge')
    discharge.set_editor_property('use_custom_primitive_data', True)
    discharge.set_editor_property('primitive_data_index', 0)
    discharge.set_editor_property('default_value', 0.0)
    shader = node(unreal.MaterialExpressionCustom)
    shader.set_editor_property('description', 'Porcelain charge dial' if analog else 'Fluorescent charge meter')
    shader.set_editor_property('code', DIAL if analog else METER)
    shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for key in ('UV', 'Discharge'):
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', key)
        inputs.append(pin)
    shader.set_editor_property('inputs', inputs)
    assert edit.connect_material_expressions(uv, '', shader, 'UV')
    assert edit.connect_material_expressions(discharge, '', shader, 'Discharge')
    emissive = shader
    if analog:
        assert edit.connect_material_property(shader, '', unreal.MaterialProperty.MP_BASE_COLOR)
        for key, value, prop in [('Metallic', .14, unreal.MaterialProperty.MP_METALLIC),
                                 ('Roughness', .31, unreal.MaterialProperty.MP_ROUGHNESS)]:
            param = node(unreal.MaterialExpressionScalarParameter, key)
            param.set_editor_property('default_value', value)
            assert edit.connect_material_property(param, '', prop)
        emissive = node(unreal.MaterialExpressionMultiply)
        emissive.set_editor_property('const_b', .32)
        assert edit.connect_material_expressions(shader, '', emissive, 'A')
    assert edit.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert library.save_loaded_asset(material, False)
    return material


def make_blue_finishes():
    folder = '/Game/Weapons/Railgun/Materials'
    parent = unreal.load_asset('/Game/Materials/Weapons/M_RailMetal')
    finishes = {'RailGauge': make_instrument(False)}
    for slot, tint, metal, roughness, fill in [
            ('RailCobalt', (.022, .135, .57), .34, .25, .30),
            ('RailPorcelain', (.62, .85, .93), .16, .29, .29)]:
        mi = asset('MI_' + slot, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi, parent)
        edit.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*tint, 1))
        for key, value in [('Metallic', metal), ('Roughness', roughness), ('Emission', fill)]:
            edit.set_material_instance_scalar_parameter_value(mi, key, value)
        assert library.save_loaded_asset(mi, False)
        finishes[slot] = mi
    return finishes
