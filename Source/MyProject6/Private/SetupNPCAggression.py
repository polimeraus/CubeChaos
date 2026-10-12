"""Persist Black Monster/Clown tuning in Blueprints and reconcile placed instances.

Run after building MyProject6Editor, using an isolated Unreal Python commandlet.
Backups and a settings report are saved under Saved/NPCAggression.
"""
from datetime import datetime
from pathlib import Path
import json
import shutil
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
BLACK = '/Game/Characters/black-monster/BP_BlackMonster_Follower'
CLOWN = '/Game/Characters/clown_monster/BP_ClownMonster_NPC'
BLEND = '/Game/Characters/clown_monster/Animations/BS_ClownMonster_Locomotion'
WHISPER = '/Game/Characters/Whisper/BP_Whisper_Follower'
CONTENT = Path(unreal.Paths.project_content_dir())
OUT = Path(unreal.Paths.project_saved_dir()) / 'NPCAggression'
BACKUP = OUT / 'Backup' / datetime.now().strftime('%Y%m%d_%H%M%S')

def backup_file(source):
    destination = BACKUP / source.relative_to(CONTENT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)

for path in [BLACK, CLOWN, BLEND]:
    backup_file(CONTENT / (path.removeprefix('/Game/') + '.uasset'))
backup_file(CONTENT / 'FirstPerson/Lvl_FirstPerson.umap')
for source in (CONTENT / '__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'):
    backup_file(source)

assert unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
whisper_bp = unreal.load_asset(WHISPER)
whisper = unreal.get_default_object(whisper_bp.generated_class())
reference = whisper.get_component_by_class(unreal.NPCHitReactionComponent)
bend_settings = {key: reference.get_editor_property(key) for key in
                 ['forward_bend_angle', 'forward_bend_attack_time', 'stagger_duration']}
assert bend_settings['forward_bend_angle'] == 70.0

def set_properties(obj, values):
    obj.modify()
    for key, value in values.items():
        if obj.get_editor_property(key) != value:
            obj.set_editor_property(key, value)

def tune_black(obj):
    set_properties(obj.get_component_by_class(unreal.NPCHitReactionComponent), bend_settings)

clown_settings = {'contact_attack_play_rate': 1.8,
                  'follow_repath_interval': .1}
movement_settings = {'max_walk_speed': 540.0, 'max_acceleration': 3600.0,
                     'braking_deceleration_walking': 3600.0,
                     'rotation_rate': unreal.Rotator(pitch=0, yaw=900, roll=0)}

def tune_clown(obj):
    values = dict(clown_settings)
    if obj != unreal.get_default_object(obj.get_class()):
        # EditDefaultsOnly is inherited directly, and is not writable on actors.
        values.pop('follow_repath_interval')
    set_properties(obj, values)
    set_properties(obj.character_movement, movement_settings)

blueprints = {BLACK: unreal.load_asset(BLACK), CLOWN: unreal.load_asset(CLOWN)}
blend = unreal.load_asset(BLEND)
blend.modify()
parameters = list(blend.get_editor_property('blend_parameters'))
parameters[0].set_editor_property('max', 540.0)
blend.set_editor_property('blend_parameters', parameters)
samples = list(blend.get_editor_property('sample_data'))
for sample in samples:
    name = sample.get_editor_property('animation').get_name()
    speed, rate = {'anim_idle_1': (0.0, 1.0), 'anim_Walk': (180.0, 1.2),
                   'anim_run_1': (540.0, 1.5)}[name]
    sample.set_editor_property('sample_value', unreal.Vector(speed, 0, 0))
    sample.set_editor_property('rate_scale', rate)
blend.set_editor_property('sample_data', samples)
blend.set_editor_property('target_weight_interpolation_speed_per_sec', 10.0)

for path, tune in [(BLACK, tune_black), (CLOWN, tune_clown)]:
    bp = blueprints[path]
    bp.modify()
    defaults = unreal.get_default_object(bp.generated_class())
    tune(defaults)
    if path == CLOWN:
        defaults.rebuild_locomotion_blend_space()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
assert unreal.EditorAssetLibrary.save_loaded_asset(blend, False)

updated = []
for actor in actors.get_all_level_actors():
    for path, tune in [(BLACK, tune_black), (CLOWN, tune_clown)]:
        if actor.get_class() == blueprints[path].generated_class():
            actor.modify()
            tune(actor)
            updated.append(actor.get_actor_label())

# Ghoul was a native actor: its constructor now owns the formerly instance-only
# bend and full fall, so portal spawns and death replacements inherit them too.
ghoul = unreal.get_default_object(unreal.ChasingNPC)
assert ghoul.get_editor_property('death_animation').get_name() == 'A_Ghoul_Death_FullFall'
assert ghoul.get_component_by_class(unreal.NPCHitReactionComponent).get_editor_property('forward_bend_angle') == 65.0
for actor in actors.get_all_level_actors():
    if actor.get_class() == unreal.ChasingNPC.static_class():
        actor.modify()
        set_properties(actor, {'death_animation': ghoul.get_editor_property('death_animation'),
                               'death_animation_play_rate': ghoul.get_editor_property('death_animation_play_rate')})
        set_properties(actor.get_component_by_class(unreal.NPCHitReactionComponent),
                       {'forward_bend_angle': 65.0, 'stagger_duration': .4})
        updated.append(actor.get_actor_label())
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
(OUT / 'Settings.json').write_text(json.dumps({'backup': str(BACKUP), 'black_bend': bend_settings,
    'clown': clown_settings, 'clown_movement': {k: str(v) for k,v in movement_settings.items()},
    'updated_instances': updated, 'ghoul_promoted_to_native_class': True}, indent=2))
unreal.log('NPC_AGGRESSION_SETUP_OK')
