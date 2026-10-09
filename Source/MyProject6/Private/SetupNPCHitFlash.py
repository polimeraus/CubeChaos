"""Create the shared, parameterized damage overlay without modifying NPC skin materials."""
import unreal

path = '/Game/NPC/Materials/M_NPCHitFlash'
edit = unreal.MaterialEditingLibrary
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_NPCHitFlash', '/Game/NPC/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('used_with_skeletal_mesh', True)
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name', 'HitFlashColor')
    color.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
    intensity = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    intensity.set_editor_property('parameter_name', 'HitFlashIntensity')
    intensity.set_editor_property('default_value', 2.0)
    emission = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
    assert edit.connect_material_expressions(color, 'RGB', emission, 'A')
    assert edit.connect_material_expressions(intensity, '', emission, 'B')
    assert edit.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    amount = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    amount.set_editor_property('parameter_name', 'HitFlashAmount')
    amount.set_editor_property('default_value', 0.0)
    opacity = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
    opacity.set_editor_property('const_b', .9)
    assert edit.connect_material_expressions(amount, '', opacity, 'A')
    assert edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('NPC_HIT_FLASH_MATERIAL_READY')
