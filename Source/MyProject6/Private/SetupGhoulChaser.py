"""Configure the imported UE5 Ghoul for ChasingNPC after building the editor target.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nullrhi.
Backs up packages before saving and preserves placed NPC gameplay settings.
"""
from datetime import datetime
from pathlib import Path
import shutil
import unreal


GHOUL = '/Game/NPC/Ghoul/Mesh/SK_Ghoul_UE5'
ANIMATION = '/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed'
LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
backup_dir = Path(unreal.Paths.project_saved_dir()) / 'GhoulChaserBackup' / datetime.now().strftime('%Y%m%d_%H%M%S')


def backup(package, extension='.uasset'):
    relative = Path(package.removeprefix('/Game/') + extension)
    source = Path(unreal.Paths.project_content_dir()) / relative
    destination = backup_dir / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


ghoul = unreal.load_asset(GHOUL)
animation = unreal.load_asset(ANIMATION)
if ghoul is None or animation is None:
    raise RuntimeError('Ghoul UE5 mesh or ABP_Unarmed is missing')

defaults = unreal.get_default_object(unreal.ChasingNPC)
if defaults.mesh.get_skinned_asset() != ghoul:
    raise RuntimeError('Build MyProject6Editor first: ChasingNPC must default to Ghoul so replacements inherit it')

skeleton = ghoul.get_editor_property('skeleton')
animation_skeleton = animation.get_editor_property('target_skeleton')
compatible = list(skeleton.get_editor_property('compatible_skeletons'))
if animation_skeleton not in compatible:
    backup(skeleton.get_path_name().split('.')[0])
    skeleton.add_compatible_skeleton(animation_skeleton)
    if not unreal.EditorAssetLibrary.save_loaded_asset(skeleton, False):
        raise RuntimeError('Could not save Ghoul skeleton compatibility')

if not unreal.EditorLoadingAndSavingUtils.load_map(LEVEL):
    raise RuntimeError('Could not load the gameplay map')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
npcs = [actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == 'NPC_PlayerChaser']
if len(npcs) != 1 or not isinstance(npcs[0], unreal.ChasingNPC):
    raise RuntimeError('Expected one existing NPC_PlayerChaser of class ChasingNPC')
npc = npcs[0]
mesh = npc.mesh
anim_class = animation.generated_class()
changed = mesh.get_skinned_asset() != ghoul or mesh.get_editor_property('anim_class') != anim_class
if changed:
    backup(LEVEL, '.umap')
    mesh.set_skeletal_mesh_asset(ghoul)
    mesh.set_anim_instance_class(anim_class)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError('Could not save NPC appearance')

assert mesh.get_skinned_asset() == ghoul
assert mesh.get_editor_property('anim_class') == anim_class
unreal.log('GHOUL_SETUP_OK: NPC_PlayerChaser and class defaults use Ghoul; skeleton compatibility saved')
