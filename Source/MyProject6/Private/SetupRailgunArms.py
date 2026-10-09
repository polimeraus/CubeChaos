"""Extend only the proximal arm geometry beyond the bottom of the first-person frame."""
import unreal

material = unreal.load_asset('/Game/Weapons/Railgun/M_RailArms')
assert material, 'Run SetupRailgun.py once to create the arms material'
edit = unreal.MaterialEditingLibrary
nodes = list(edit.get_material_expressions(material))
position = next(node for node in nodes if isinstance(node, unreal.MaterialExpressionPreSkinnedPosition))
extension = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
if extension is None:
    extension = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
assert isinstance(extension, unreal.MaterialExpressionCustom)
extension.set_editor_property('description', 'Extend proximal arms out of view; preserve hands')
pin = unreal.CustomInput()
pin.set_editor_property('input_name', 'Position')
extension.set_editor_property('inputs', [pin])
extension.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
# Weight is based on Manny's reference pose, independent of animation and camera pitch.
# Wrist/finger vertices are outside this interval and stay exactly on the weapon.
# Moving back AND down keeps the open end below the viewport without crossing the near plane.
extension.set_editor_property('code', 'float rearWeight = 1.0 - smoothstep(25.0, 42.0, abs(Position.x)); return (-ResolvedView.ViewForward * 12.0 - ResolvedView.ViewUp * 22.0) * rearWeight;')
assert edit.connect_material_expressions(position, '', extension, 'Position')
assert edit.connect_material_property(extension, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('RAILGUN_ARMS_EXTENDED')
