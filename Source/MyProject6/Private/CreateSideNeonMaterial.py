"""Build the shared opaque side-neon material; run with Unreal's PythonScript commandlet."""
from pathlib import Path
import unreal

folder = "/Game/Destructible/Materials"
name = "M_FloorSideNeon"
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
material.set_editor_property("two_sided", False)

inputs = []
connections = []


def add_input(name, expression, output=""):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", name)
    inputs.append(item)
    connections.append((expression, output, name))


uv = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate)
uv.set_editor_property("coordinate_index", 0)
add_input("UV", uv)
for parameter, default in {
    "GlowColor": (0.0, 4.0, 8.0, 1.0),
    "BodyColor": (0.015, 0.015, 0.02, 1.0),
    "Stripe": (0.5, 0.08, 0.0, 0.0),
}.items():
    node = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    node.set_editor_property("parameter_name", parameter)
    node.set_editor_property("default_value", unreal.LinearColor(*default))
    add_input(parameter, node, "RGBA")

custom = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
custom.set_editor_property("description", "Antialiased full side / height stripe")
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property("inputs", inputs)
source = Path(unreal.Paths.project_dir()) / "Source/MyProject6/Private/SideNeonMaterial.ush"
custom.set_editor_property("code", source.read_text())
for expression, output, parameter in connections:
    if not edit.connect_material_expressions(expression, output, custom, parameter):
        raise RuntimeError("Cannot connect " + parameter)
if not edit.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Cannot connect side neon emissive output")
edit.layout_material_expressions(material)
errors = edit.recompile_material(material)
if errors:
    raise RuntimeError("Side neon material compile errors: " + str(errors))
if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
    raise RuntimeError("Cannot save " + path)
unreal.log("SIDE_NEON_MATERIAL_OK " + path)
