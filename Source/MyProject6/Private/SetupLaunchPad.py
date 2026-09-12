"""Generate the launch-pad material and reusable Blueprint; never change a level."""
from pathlib import Path
import runpy
import unreal
runpy.run_path(str(Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private/CreateLaunchPadMaterial.py'))
path = '/Game/Gameplay/LaunchPad/BP_LaunchPad'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.log('LAUNCH_PAD_READY existing Blueprint preserved: ' + path)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.LaunchPad)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_LaunchPad', '/Game/Gameplay/LaunchPad', unreal.Blueprint, factory)
    if not bp:
        raise RuntimeError('Could not create launch pad Blueprint')
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.get_default_object(bp.generated_class()).set_editor_property('pad_material', unreal.load_asset('/Game/Gameplay/LaunchPad/M_LaunchPad'))
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp, False):
        raise RuntimeError('Could not save launch pad Blueprint')
    unreal.log('LAUNCH_PAD_READY ' + path)
