"""Create the opaque, unlit material used to light complete destructible pieces."""
import unreal

folder = "/Game/Materials/Structures"
name = "M_MusicPieceGlow"
path = f"{folder}/{name}"
material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, unreal.MaterialFactoryNew())
if material is None:
    raise RuntimeError("Cannot create " + path)
edit = unreal.MaterialEditingLibrary
edit.delete_all_material_expressions(material)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(.03, .45, 1, 1))
# The actor supplies Color multiplied by the current intensity, once per music update.
if not edit.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Cannot connect music emissive color")
edit.layout_material_expressions(material)
errors = edit.recompile_material(material)
if errors:
    raise RuntimeError("Music piece material compile errors: " + str(errors))
if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
    raise RuntimeError("Cannot save " + path)
unreal.log("MUSIC_PIECE_MATERIAL_OK " + path)
