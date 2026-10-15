"""Create the one reusable Fantasy House Blueprint and its palette material.

Run in UnrealEditor-Cmd with -run=pythonscript -script=<this file> -nullrhi.
Existing assets are preserved. Scene placement is performed separately.
"""
import json
from pathlib import Path
import unreal

folder = '/Game/Structures/DynamicMesh/FantasyHouse'
assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
path = folder + '/Materials/M_FantasyHouse'
mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if mat is None:
    mat = assets.create_asset('M_FantasyHouse', folder+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
    tint = edit.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(.36, .24, .51, 1))
    glow = edit.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 200)
    glow.set_editor_property('parameter_name', 'Glow')
    multiply = edit.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 150)
    edit.connect_material_expressions(tint, '', multiply, 'A')
    edit.connect_material_expressions(glow, '', multiply, 'B')
    edit.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(multiply, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = edit.create_material_expression(mat, unreal.MaterialExpressionConstant, -250, 330)
    rough.set_editor_property('r', .68)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    metallic = edit.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -250, 440)
    metallic.set_editor_property('parameter_name', 'Metallic')
    metallic.set_editor_property('default_value', .05)
    edit.connect_material_property(metallic, '', unreal.MaterialProperty.MP_METALLIC)
    edit.layout_material_expressions(mat)
    assert not edit.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat, False)

bp_path = folder + '/BP_ParametricFantasyHouse'
if not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ParametricFantasyHouse)
    bp = assets.create_asset('BP_ParametricFantasyHouse', folder, unreal.Blueprint, factory)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property('base_material', mat)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
else:
    bp = unreal.load_asset(bp_path)

out = Path(unreal.Paths.project_saved_dir()) / 'FantasyHouse'
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
layout = []
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if any(s in label.lower() for s in ('house', 'floor', 'playerstart', 'cone', 'platform', 'fantasy')):
        origin, extent = actor.get_actor_bounds(False)
        layout.append({'label': label, 'class': actor.get_class().get_name(),
                       'location': str(actor.get_actor_location()),
                       'origin': [origin.x, origin.y, origin.z], 'extent': [extent.x, extent.y, extent.z]})
(out / 'Layout.json').write_text(json.dumps(layout, indent=2))
unreal.log('FANTASY_HOUSE_SETUP_OK ' + bp_path)
