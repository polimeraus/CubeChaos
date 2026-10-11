"""Create the shared post-process input graph; native code bends each NPC's own spine.

No NPC mesh, existing animation, Blueprint defaults or placed actors are rewritten.
The shared reaction component installs this graph for every NPC at BeginPlay.
"""
from datetime import datetime
from pathlib import Path
import shutil
import unreal

path = '/Game/NPC/Animations/ABP_NPCHitBend'
blueprint = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if blueprint:
    source = Path(unreal.Paths.project_content_dir(), 'NPC/Animations/ABP_NPCHitBend.uasset')
    backup = Path(unreal.Paths.project_saved_dir(), 'NPCForwardBend/Backup', datetime.now().strftime('%Y%m%d_%H%M%S'))
    backup.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, backup / source.name)
else:
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property('parent_class', unreal.NPCHitBendAnimInstance)
    mesh = unreal.load_asset('/Game/Characters/Ghoul/Mesh/SK_Ghoul_UE5')
    factory.set_editor_property('target_skeleton', mesh.skeleton)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'ABP_NPCHitBend', '/Game/NPC/Animations', unreal.AnimBlueprint, factory)
assert blueprint
assert unreal.NPCHitBendAnimInstance.configure_input_pose(blueprint)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
assert blueprint.generated_class()
unreal.log('NPC_FORWARD_BEND_SETUP_OK')
