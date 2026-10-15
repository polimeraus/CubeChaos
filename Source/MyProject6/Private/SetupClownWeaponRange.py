"""Recompile the Clown class after removing advance attacks; save its instances.

Run with Unreal Python after rebuilding the editor. No other NPC is retuned.
"""
from datetime import datetime
from pathlib import Path
import json
import shutil
import unreal

root = '/Game/NPC/ClownMonster'
content = Path(unreal.Paths.project_content_dir())
output = Path(unreal.Paths.project_saved_dir()) / 'ClownRangeFix'
backup = output / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')
files = [content / 'NPC/ClownMonster/BP_ClownMonster_NPC.uasset',
         content / 'FirstPerson/Lvl_FirstPerson.umap']
files += list((content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
for source in files:
    destination = backup / source.relative_to(content)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
assert unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
bp = unreal.load_asset(root + '/BP_ClownMonster_NPC')
bp.modify()
defaults = unreal.get_default_object(bp.generated_class())
defaults.set_editor_property('contact_interaction_radius', 140.0)
defaults.set_editor_property('contact_attack_play_rate', 1.8)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
labels = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_class() == bp.generated_class():
        actor.modify()
        actor.set_editor_property('contact_interaction_radius', 140.0)
        actor.set_editor_property('contact_attack_play_rate', 1.8)
        labels.append(actor.get_actor_label())
assert labels, 'Expected the placed Clown NPC'
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
output.mkdir(parents=True, exist_ok=True)
(output / 'Settings.json').write_text(json.dumps({'class': bp.get_path_name(),
    'instances': labels, 'radius': 140, 'advance_attack_removed': True, 'backup': str(backup)}, indent=2))
unreal.log('CLOWN_WEAPON_RANGE_SAVED')
