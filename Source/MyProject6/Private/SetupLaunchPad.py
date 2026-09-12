"""Generate the launch-pad material and reusable Blueprint; never change a level."""
from pathlib import Path
import runpy
import unreal
runpy.run_path(str(Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private/CreateLaunchPadMaterial.py'))
runpy.run_path(str(Path(unreal.Paths.project_dir()) / 'Source/MyProject6/Private/CreateLaunchPadMaterial.py'), init_globals={'MATERIAL_NAME': 'M_LaunchPad_Rings', 'SHADER_SOURCE': 'LaunchPadRingsMaterial.ush'})
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

# Update the requested Blueprint defaults, preserving unrelated settings and level instances.
bp = unreal.load_asset(path)
defaults = unreal.get_default_object(bp.generated_class())
defaults.set_editor_property('rings_material', unreal.load_asset('/Game/Gameplay/LaunchPad/M_LaunchPad_Rings'))
defaults.set_editor_property('shape', unreal.LaunchPadShape.CIRCLE)
defaults.set_editor_property('pattern', unreal.LaunchPadPattern.CONVERGING_RINGS)
defaults.set_editor_property('velocity_mode', unreal.LaunchPadVelocityMode.SPEED_AND_ANGLE)
defaults.set_editor_property('launch_angle', 90)
defaults.set_editor_property('preserve_horizontal_velocity', False)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
