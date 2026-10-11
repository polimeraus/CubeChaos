"""Create a lightweight emissive spiral material and a red instance.

Color and GlowIntensity are independent of scene lighting. Existing assets are
left intact so running this recipe again preserves material customizations.
"""
import unreal

folder = '/Game/Destructible/Materials'
edit = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
path = folder + '/M_SpiralPlatformGlow'
material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material is None:
    material = assets.create_asset('M_SpiralPlatformGlow', folder, unreal.Material, unreal.MaterialFactoryNew())
    assert material, 'Cannot create spiral glow material'
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
    intensity = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    intensity.set_editor_property('parameter_name', 'GlowIntensity')
    intensity.set_editor_property('default_value', 8.)
    multiply = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
    assert edit.connect_material_expressions(color, '', multiply, 'A')
    assert edit.connect_material_expressions(intensity, '', multiply, 'B')
    assert edit.connect_material_property(multiply, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(material)
    errors = edit.recompile_material(material)
    assert not errors, str(errors)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)

instance_path = folder + '/MI_SpiralPlatformGlow_Red'
instance = unreal.load_asset(instance_path) if unreal.EditorAssetLibrary.does_asset_exist(instance_path) else None
if instance is None:
    instance = assets.create_asset('MI_SpiralPlatformGlow_Red', folder,
        unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    assert instance, 'Cannot create red spiral glow instance'
    edit.set_material_instance_parent(instance, material)
    # UE 5.8's setter always returns false; verify the applied value instead.
    red = unreal.LinearColor(1, 0, 0, 1)
    edit.set_material_instance_vector_parameter_value(instance, 'Color', red)
    assert edit.get_material_instance_vector_parameter_value(instance, 'Color') == red
    assert unreal.EditorAssetLibrary.save_loaded_asset(instance, False)
unreal.log('SPIRAL_GLOW_MATERIAL_READY ' + instance_path)
