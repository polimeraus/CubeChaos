"""Opaque unlit neon for instanced cube edges, independent of particle vertex colors."""
import unreal

folder = '/Game/Structures/Parametric/VoxelShape/Materials'
path = folder + '/M_VoxelShapeGlow'
edit = unreal.MaterialEditingLibrary
if unreal.EditorAssetLibrary.does_asset_exist(path):
    material = unreal.load_asset(path)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_VoxelShapeGlow', folder, unreal.Material, unreal.MaterialFactoryNew())
assert material
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
material.set_editor_property('used_with_instanced_static_meshes', True)
if not edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(5., .125, 1.6, 1.))
    assert edit.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.layout_material_expressions(material)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('VOXEL_SHAPE_MATERIAL_READY ' + path)
