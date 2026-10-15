"""Restore the arm's actual anatomy; framing is handled by the authored elbow pose."""
import unreal

material = unreal.load_asset('/Game/Weapons/Railgun/Materials/M_RailArms')
assert material, 'Run SetupRailgun.py once to create the arms material'
edit = unreal.MaterialEditingLibrary
mask = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK)
# The tucked elbow is below the frame. Remove the upper-arm sliver that curves
# back toward the shoulder, keeping the complete visible forearm and hand.
mask.set_editor_property('code', 'return (abs(Position.x) > 32.0 && Position.z > 65.0) ? 1.0 : 0.0;')
extension = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
if extension is not None:
    assert isinstance(extension, unreal.MaterialExpressionCustom)
    # Python holds the node rooted; deleting it here asserts in the editor. Neutralize
    # its output instead. This also makes rerunning the migration safe.
    extension.set_editor_property('code', 'return float3(0.0, 0.0, 0.0);')
    extension.set_editor_property('description', 'Natural arm geometry - no vertex stretch')
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log('RAILGUN_ARM_ANATOMY_RESTORED')
