"""Upgrade the pickup Blueprint's native defaults without changing any level or material."""
from datetime import datetime
from pathlib import Path
import shutil
import unreal

path = '/Game/Gameplay/Pickups/BP_ParametricPickup'
bp = unreal.load_asset(path)
assert bp, 'Run SetupParametricPickup.py first'
source = Path(unreal.Paths.project_content_dir()) / 'Gameplay/Pickups/BP_ParametricPickup.uasset'
backup = Path(unreal.Paths.project_saved_dir()) / 'Pickups/Backups' / datetime.now().strftime('%Y%m%d_%H%M%S')
backup.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, backup / source.name)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
defaults = unreal.get_default_object(bp.generated_class())
native = unreal.get_default_object(unreal.ParametricPickup)
# New native properties should be inherited; preserve any explicit user override.
sound = defaults.get_editor_property('collection_sound')
if not sound:
    defaults.set_editor_property('collection_sound', native.get_editor_property('collection_sound'))
assert defaults.get_editor_property('collection_sound'), 'No default collection sound'
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
unreal.log('PICKUP_MOTION_SOUND_READY ' + str(defaults.get_editor_property('motion'))
           + ' sound=' + defaults.get_editor_property('collection_sound').get_path_name())
