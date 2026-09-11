"""Upgrade the existing RadicalMike blueprint without replacing placed NPCs.

Run with UE PythonScriptPlugin. A pre-change copy of the blueprint is kept in Saved.
"""
from pathlib import Path
import shutil
import unreal

root = '/Game/Characters/RadicalMike'
material_path = root + '/Materials/M_BusterEnergy'
material = unreal.load_asset(material_path) if unreal.EditorAssetLibrary.does_asset_exist(material_path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_BusterEnergy', root + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -200, 0)
    color.set_editor_property('constant', unreal.LinearColor(0.15, 5.0, 12.0, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material)

if hasattr(unreal, 'RadicalMikeNPC'):
    project = Path(unreal.Paths.project_dir())
    source = project / 'Content/Characters/RadicalMike/BP_RadicalMike_NPC.uasset'
    backup = project / 'Saved/RadicalMikeWeapon/Before/BP_RadicalMike_NPC.uasset'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(source, backup)
    bp = unreal.load_asset(root + '/BP_RadicalMike_NPC')
    assert bp is not None
    unreal.BlueprintEditorLibrary.reparent_blueprint(bp, unreal.RadicalMikeNPC)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    assert isinstance(cdo, unreal.RadicalMikeNPC)
    cdo.set_editor_property('enable_shooting', True)
    cdo.set_editor_property('fire_interval', 2.5)
    cdo.set_editor_property('fire_range', 2500.0)
    cdo.set_editor_property('shot_energy_damage', 10.0)
    cdo.set_editor_property('projectile_speed', 2200.0)
    cdo.set_editor_property('death_fade_materials', [])
    assert cdo.buster_weapon.static_mesh.get_name() == 'SM_RadicalBuster'
    assert str(cdo.buster_weapon.get_attach_socket_name()) == 'Buster_RSocket'
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    npcs = [a for a in actors if isinstance(a, unreal.RadicalMikeNPC)]
    assert npcs, 'No existing RadicalMike actors found in the game map'
    for npc in npcs:
        assert npc.buster_weapon.static_mesh.get_name() == 'SM_RadicalBuster'
        unreal.log('RADICAL_ARMED ' + npc.get_actor_label() + ' ' + str(npc.get_actor_location()))
    unreal.log('RADICAL_WEAPON_SETUP_OK count=' + str(len(npcs)))
else:
    unreal.log('RADICAL_MATERIAL_READY; compile the new native classes and run again.')
