"""Create the input-pose graph used by the native third-person upper-body overlay."""
import unreal

folder = '/Game/Characters/FirstPerson/Animations'
name = 'ABP_ThirdPersonWeaponPose'
if not unreal.EditorAssetLibrary.does_asset_exist(folder + '/' + name):
    mesh = unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property('parent_class', unreal.ThirdPersonWeaponPose)
    factory.set_editor_property('target_skeleton', mesh.get_editor_property('skeleton'))
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.AnimBlueprint, factory)
    assert blueprint and unreal.NPCHitBendAnimInstance.configure_input_pose(blueprint)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert blueprint.generated_class() and unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
