"""Persist the brighter rail beam in player defaults and reconcile placed overrides."""
from pathlib import Path
import json
import shutil
import unreal

root = Path(unreal.Paths.project_dir())
out = root / 'Saved/Railgun/Visibility'
out.mkdir(parents=True, exist_ok=True)
source = root / 'Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset'
backup = out / 'BP_FirstPersonCharacter.before.uasset'
if not backup.exists():
    shutil.copy2(source, backup)

native = unreal.RailgunVisualSettings()
settings = {key: native.get_editor_property(key)
            for key in ['intensity', 'density', 'width', 'spiral_width']}

def update_visuals(obj, property_name):
    visuals = obj.get_editor_property(property_name)
    if all(abs(visuals.get_editor_property(key)-value) < .001 for key, value in settings.items()):
        return False
    for key, value in settings.items():
        visuals.set_editor_property(key, value)
    obj.set_editor_property(property_name, visuals)
    return True

bp = unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
assert bp
cdo = unreal.get_default_object(bp.generated_class())
update_visuals(cdo, 'railgun_visuals')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
updated = []
for actor in actors:
    changed = False
    if isinstance(actor, unreal.MyProject6Character):
        changed = update_visuals(actor, 'railgun_visuals')
    elif isinstance(actor, unreal.RailgunWeapon):
        changed = update_visuals(actor, 'visuals')
        duration = unreal.get_default_object(unreal.RailgunWeapon).trail_duration
        if abs(actor.trail_duration-duration) > .001:
            actor.set_editor_property('trail_duration', duration)
            changed = True
    if changed:
        updated.append(actor.get_actor_label())
if updated:
    assert level.save_current_level()
(out/'Setup.json').write_text(json.dumps({'settings': settings, 'updated_instances': updated}, indent=2))
unreal.log('RAILGUN_VISIBILITY_SAVED')
