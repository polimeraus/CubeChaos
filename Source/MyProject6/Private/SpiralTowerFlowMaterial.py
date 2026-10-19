"""A full-height vertical light strip follows the stairs. Fade only along the path."""
import unreal


def create_material(update_existing=False):
    folder = '/Game/Structures/Destructible/SpiralTower/Materials'
    name = 'M_SpiralTowerFlowGlow'
    edit = unreal.MaterialEditingLibrary
    path = folder + '/' + name
    material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if material and not update_existing:
        return material
    if material:
        material.modify()
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
    # Native constructor references root this asset and its existing expressions.
    # Update nodes in place; deleting rooted expressions asserts in the editor.
    expressions = list(edit.get_material_expressions(material))

    def expression(cls, x, y, parameter=None):
        previous = {'PathLength': 'TravelHeight', 'TrailLength': 'BaseHeight'}.get(parameter)
        node = next((e for e in expressions if isinstance(e, cls) and
                     (parameter is None or str(e.get_editor_property('parameter_name')) in (parameter, previous))), None)
        return node or edit.create_material_expression(material, cls, x, y)

    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)
    path_uv = expression(unreal.MaterialExpressionTextureCoordinate, -650, -300)
    path_uv.set_editor_property('coordinate_index', 1)
    inputs = {'Path': path_uv}
    inputs['Clock'] = expression(unreal.MaterialExpressionTime, -650, -150)
    defaults = dict(BandHeight=900., TrailLength=600., PathLength=41700., Speed=500., Pause=1.2,
                    Loop=1., Brightness=6., StartTime=0., PreviewTime=-1.)
    for index, (key, value) in enumerate(defaults.items()):
        node = expression(unreal.MaterialExpressionScalarParameter, -650, index * 100, key)
        node.set_editor_property('parameter_name', key)
        node.set_editor_property('default_value', value)
        inputs[key] = node
    tint = expression(unreal.MaterialExpressionVectorParameter, -650, 950, 'Tint')
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(.025, .55, 1., 1.))
    inputs['Tint'] = tint
    shader = expression(unreal.MaterialExpressionCustom, -100, 0)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    shader.set_editor_property('inputs', pins)
    shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    shader.set_editor_property('description', 'Vertical strip follows the stairs; every wall height brightens and fades together')
    shader.set_editor_property('code', '''
float trail = max(TrailLength, 10.0);
float speed = max(Speed, 1.0);
float duration = (max(PathLength, 1.0) + trail) / speed;
float elapsed = PreviewTime >= 0.0 ? PreviewTime : max(Clock - StartTime, 0.0);
float phase = Loop > 0.5 ? fmod(elapsed, duration + max(Pause, 0.0)) : elapsed;
float center = -trail * 0.5 + phase * speed;
// Only progress along the spiral affects the light. All heights of the matching
// wall column share the same phase and brightness, including its stair tread.
float envelope = saturate(1.0 - abs(Path.x - center) / (trail * 0.5));
envelope = envelope * envelope * (3.0 - 2.0 * envelope);
float active = phase < duration ? 1.0 : 0.0;
return max(Tint.rgb, 0.0) * max(Brightness, 0.0) * envelope * active;
''')
    for key, node in inputs.items():
        assert edit.connect_material_expressions(node, '', shader, key), key
    assert edit.connect_material_property(shader, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = expression(unreal.MaterialExpressionConstant, -100, 500)
    opacity.set_editor_property('r', 1.)
    assert edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    return material


if __name__ == '__main__':
    create_material(update_existing=True)
