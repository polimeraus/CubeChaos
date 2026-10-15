"""Apply restrained impact reactions to NPC Blueprints and existing level instances.

Run in an isolated Unreal Python commandlet after building the editor target.
On a fresh checkout, run SetupNPCHitFlash.py first to create the shared flash material.
Changed assets/maps are backed up before saving.
"""
from datetime import datetime
from pathlib import Path
import json
import shutil
import unreal

content = Path(unreal.Paths.project_content_dir())
output = Path(unreal.Paths.project_saved_dir()) / 'NPCHitReaction'
backup = output / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')
settings = {'stagger_distance':28.0, 'stagger_duration':.34, 'forward_bend_angle':42.0,
            'forward_bend_attack_time':.065, 'stagger_compression':2.0}


def backup_file(source):
    if source.is_file():
        destination = backup / source.relative_to(content)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def configure(actor):
    component = actor.get_component_by_class(unreal.NPCHitReactionComponent)
    if component is None:
        raise RuntimeError('Missing hit reaction on ' + actor.get_path_name())
    changes = {key:value for key, value in settings.items() if abs(component.get_editor_property(key)-value) > .0001}
    if changes:
        actor.modify()
        component.modify()
        for key, value in changes.items():
            component.set_editor_property(key, value)
    return bool(changes)


report = {'blueprints':[], 'placed':[], 'backup':str(backup)}
for path in ['/Game/NPC/RadicalMike/BP_RadicalMike_NPC',
             '/Game/NPC/Whisper/BP_Whisper_Follower',
             '/Game/NPC/BlackMonster/BP_BlackMonster_Follower']:
    blueprint = unreal.load_asset(path)
    if blueprint is None:
        raise RuntimeError('Missing NPC Blueprint: ' + path)
    backup_file(content / (path.removeprefix('/Game/') + '.uasset'))
    changed = configure(unreal.get_default_object(blueprint.generated_class()))
    if changed:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
    report['blueprints'].append({'path':path,'changed':changed})

level = '/Game/FirstPerson/Lvl_FirstPerson'
assert unreal.EditorLoadingAndSavingUtils.load_map(level)
backup_file(content / 'FirstPerson/Lvl_FirstPerson.umap')
for source in (content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'):
    backup_file(source)
changed = False
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, (unreal.ChasingNPC, unreal.WhisperFollower)):
        updated = configure(actor)
        changed = updated or changed
        reaction = actor.get_component_by_class(unreal.NPCHitReactionComponent)
        report['placed'].append({'name':actor.get_actor_label(),'changed':updated,
                                 'distance':reaction.get_editor_property('stagger_distance')})
if changed:
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
output.mkdir(parents=True, exist_ok=True)
(output / 'setup.json').write_text(json.dumps(report, indent=2))
unreal.log('NPC_RECOIL_SETUP_OK ' + json.dumps(report))
