"""Create Ghoul's contact swipe from the compatible unarmed attack, with open claws.

Run once with UnrealEditor-Cmd -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi.
Existing generated assets and the source animation are preserved.
"""
import unreal

source_path = '/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02'
target_path = '/Game/NPC/Ghoul/Animations/Anim_Ghoul_Claw'

if not unreal.EditorAssetLibrary.does_asset_exist(target_path):
    animation = unreal.EditorAssetLibrary.duplicate_asset(source_path, target_path)
    if animation is None:
        raise RuntimeError('Could not duplicate the compatible Ghoul attack')
    skeleton = animation.get_editor_property('skeleton')
    reference = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    controller = animation.controller
    controller.open_bracket('Open Ghoul claws for the contact swipe', False)
    try:
        # Keep the broad arm sweep; replace closed fists with the skeleton's open hand pose.
        for side in ['l', 'r']:
            for finger in ['thumb', 'index', 'middle', 'ring', 'pinky']:
                for joint in range(1, 4):
                    bone = f'{finger}_0{joint}_{side}'
                    pose = unreal.AnimPoseExtensions.get_bone_pose(reference, bone, unreal.AnimPoseSpaces.LOCAL)
                    if not controller.set_bone_track_keys(bone, [pose.translation], [pose.rotation], [pose.scale3d], False):
                        raise RuntimeError('Could not open claw joint ' + bone)
    finally:
        controller.close_bracket(False)
    if not unreal.EditorAssetLibrary.save_loaded_asset(animation, False):
        raise RuntimeError('Could not save the Ghoul claw animation')
unreal.log('GHOUL_CONTACT_ANIMATION_OK ' + target_path)
