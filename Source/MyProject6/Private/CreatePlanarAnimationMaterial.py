"""Build the shared additive surface material from the versioned HLSL source."""
from pathlib import Path
import unreal

folder = "/Game/Destructible/Materials"
name = "M_PlanarAnimation"
path = f"{folder}/{name}"
material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
if material is None:
    raise RuntimeError(f"Cannot create {path}")

edit = unreal.MaterialEditingLibrary
edit.delete_all_material_expressions(material)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", False)
material.set_editor_property("disable_depth_test", False)

def node(cls):
    return edit.create_material_expression(material, cls)

inputs = []
connections = []

def add_input(name, expression, output=""):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", name)
    inputs.append(item)
    connections.append((expression, output, name))

uv = node(unreal.MaterialExpressionTextureCoordinate)
uv.set_editor_property("coordinate_index", 1)
add_input("UV", uv)

for name, default in {
    "Flow": (1, 0, 0, 0), "Footprint": (1000, 1000, .5, .5),
    "Fixed": (0, 0, .1, 5), "Spiral": (5, .25, 1, 0),
    "WaveColor": (0, 0, 0, 0), "MusicColor": (0, 0, 0, 0),
    **{f"Waves{i}": (-1, -1, -1, -1) for i in range(8)},
}.items():
    expression = node(unreal.MaterialExpressionVectorParameter)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", unreal.LinearColor(*default))
    add_input(name, expression, "RGBA")

for name, default in {"MusicEnabled": 0, "SpiralDirection": 1}.items():
    expression = node(unreal.MaterialExpressionScalarParameter)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", default)
    add_input(name, expression)

texture = node(unreal.MaterialExpressionTextureObjectParameter)
texture.set_editor_property("parameter_name", "MusicMask")
texture.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
add_input("MusicMask", texture)
custom = node(unreal.MaterialExpressionCustom)
custom.set_editor_property("description", "Shared planar fixed and music surface mask")
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property("inputs", inputs)
source = Path(unreal.Paths.project_dir()) / "Source/MyProject6/Private/PlanarAnimationMaterial.ush"
custom.set_editor_property("code", source.read_text())
for expression, output, name in connections:
    if not edit.connect_material_expressions(expression, output, custom, name):
        raise RuntimeError(f"Failed to connect {name}")
edit.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
opacity = node(unreal.MaterialExpressionConstant)
opacity.set_editor_property("r", 1.0)
edit.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
edit.layout_material_expressions(material)
errors = edit.recompile_material(material)
if errors:
    raise RuntimeError("Planar shader compile errors: " + str(errors))
if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
    raise RuntimeError(f"Could not save {path}")
unreal.log("PLANAR_ANIMATION_MATERIAL_OK " + path)
