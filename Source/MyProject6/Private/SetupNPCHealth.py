"""Apply NPC health defaults to existing blueprints and placed gameplay actors.

Run after building MyProject6Editor with -run=pythonscript and PythonScriptPlugin.
Only changed packages are saved; original packages are backed up under Saved.
"""
from datetime import datetime
from pathlib import Path
import json
import shutil
import unreal


LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
content = Path(unreal.Paths.project_content_dir())
output = Path(unreal.Paths.project_saved_dir()) / 'NPCHealth'
backup = output / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')


def backup_package(package, extension='.uasset'):
    relative = Path(package.removeprefix('/Game/') + extension)
    source = content / relative
    if source.is_file():
        destination = backup / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def configure(actor, health):
    changed = False
    for name, value in [('max_health', health), ('can_be_damaged', True)]:
        if actor.get_editor_property(name) != value:
            actor.set_editor_property(name, value)
            changed = True
    return changed


for path, health in [('/Game/NPC/RadicalMike/BP_RadicalMike_NPC', 200.0),
                     ('/Game/NPC/Whisper/BP_Whisper_Follower', 150.0)]:
    bp = unreal.load_asset(path)
    assert bp, path
    backup_package(path)
    defaults = unreal.get_default_object(bp.generated_class())
    changed = configure(defaults, health)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    defaults = unreal.get_default_object(bp.generated_class())
    assert defaults.get_editor_property('max_health') == health
    assert defaults.get_editor_property('can_be_damaged')
    assert defaults.get_component_by_class(unreal.NPCHealthBarComponent)
    if changed:
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

assert unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
backup_package(LEVEL, '.umap')
for external in (content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'):
    destination = backup / external.relative_to(content)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(external, destination)

report = []
changed = False
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, unreal.RadicalMikeNPC):
        health = 200.0
    elif isinstance(actor, unreal.ChasingNPC):
        health = 100.0
    elif isinstance(actor, unreal.WhisperFollower):
        health = 150.0
    else:
        continue
    changed = configure(actor, health) or changed
    assert actor.get_component_by_class(unreal.NPCHealthBarComponent)
    report.append(dict(actor=actor.get_actor_label(), health=health))

if changed:
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)

yellow = unreal.get_default_object(unreal.BallProjectile).get_editor_property('actor_damage')
red = unreal.get_default_object(unreal.CannonballProjectile).get_editor_property('actor_damage')
assert yellow == 25.0 and red == yellow * 2
output.mkdir(parents=True, exist_ok=True)
(output / 'setup.json').write_text(json.dumps(dict(npcs=report, yellow_damage=yellow, red_damage=red,
                                                 backup=str(backup)), indent=2))
unreal.log('NPC_HEALTH_SETUP_OK ' + json.dumps(report))
