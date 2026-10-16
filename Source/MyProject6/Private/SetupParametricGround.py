"""Create the reusable ground material and Blueprint, preserving existing assets and maps."""
import unreal

root = '/Game/GameplayActors/Ground'
material_path = root + '/Materials/M_ParametricGround'
assets = unreal.AssetToolsHelpers.get_asset_tools()
if not unreal.EditorAssetLibrary.does_asset_exist(material_path):
    material = assets.create_asset('M_ParametricGround', root + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    assert material, 'Ground material creation failed'
    edit = unreal.MaterialEditingLibrary
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(0.075, 0.09, 0.115, 1))
    assert edit.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    roughness.set_editor_property('parameter_name', 'Roughness')
    roughness.set_editor_property('default_value', 0.85)
    assert edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)

# The first pass can run before the native class is compiled, so its constructor can find the material.
ground_class = getattr(unreal, 'ParametricGround', None)
if ground_class:
    bp_path = root + '/BP_ParametricGround'
    if not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', ground_class)
        blueprint = assets.create_asset('BP_ParametricGround', root, unreal.Blueprint, factory)
        assert blueprint, 'Ground Blueprint creation failed'
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
    unreal.log('PARAMETRIC_GROUND_READY ' + bp_path)
else:
    unreal.log('PARAMETRIC_GROUND_MATERIAL_READY; run again after compiling the native class')
