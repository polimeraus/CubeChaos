"""Load saved packages in a fresh Unreal process and compare classes and placements."""
import json
from pathlib import Path
import unreal

ROOT = '/Game/Characters/'
paths = {'black': ROOT + 'black-monster/BP_BlackMonster_Follower',
         'clown': ROOT + 'clown_monster/BP_ClownMonster_NPC',
         'whisper': ROOT + 'Whisper/BP_Whisper_Follower'}
classes = {k: unreal.load_asset(v).generated_class() for k,v in paths.items()}
classes['ghoul'] = unreal.ChasingNPC.static_class()
defaults = {k: unreal.get_default_object(v) for k,v in classes.items()}
checks = []
def check(ok, label):
    assert ok, label
    checks.append(label)
def reaction(a, prop):
    return a.get_component_by_class(unreal.NPCHitReactionComponent).get_editor_property(prop)
def verify(k, obj):
    cdo = defaults[k]
    for prop in ['forward_bend_angle','forward_bend_attack_time','stagger_duration','stagger_distance']:
        check(reaction(obj, prop) == reaction(cdo, prop), k + ' inherits ' + prop)
    for prop in ['death_animation','contact_attack_animation','contact_attack_play_rate']:
        check(obj.get_editor_property(prop) == cdo.get_editor_property(prop), k + ' inherits ' + prop)
    if k == 'clown':
        check(abs(obj.get_editor_property('contact_attack_play_rate') - 1.8) < .001, 'Clown attack 1.8x')
        check(abs(obj.get_editor_property('follow_repath_interval') - .1) < .001, 'Clown rapid path updates')
        check(obj.character_movement.max_walk_speed == 540, 'Clown speed 540cm/s')
        check(obj.character_movement.max_acceleration == 3600, 'Clown acceleration 3600cm/s2')
        check(obj.character_movement.rotation_rate.yaw == 900, 'Clown faster turning')
    elif k in ['black', 'whisper']:
        check(reaction(obj, 'forward_bend_angle') == 70, k + ' strong 70 degree bend')
        check(abs(reaction(obj, 'stagger_duration') - .42) < .001, k + ' recovery .42 seconds')
    else:
        check(reaction(obj, 'forward_bend_angle') == 65, 'Ghoul class has strong bend')
        check(obj.get_editor_property('death_animation').get_name() == 'A_Ghoul_Death_FullFall', 'Ghoul class has full fall')

for k, cdo in defaults.items(): verify(k, cdo)
assert unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
placed = []
for obj in actors.get_all_level_actors():
    for k, cls in classes.items():
        if obj.get_class() == cls:
            verify(k, obj)
            placed.append(obj.get_actor_label())
check(len(placed) >= 4, 'All four placed NPCs checked')
spawn_defaults = unreal.get_default_object(unreal.load_asset('/Game/NPC/Spawning/BP_NPCSpawnPoint').generated_class())
check(spawn_defaults.get_editor_property('npc_types').get('ClownMonster') == classes['clown'], 'Portal uses the tuned Clown class')
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
for i, (k, cls) in enumerate(classes.items()):
    spawned = actors.spawn_actor_from_class(cls, unreal.Vector(i * 500, 0, 200))
    verify(k, spawned)
blend = unreal.load_asset(ROOT + 'clown_monster/Animations/BS_ClownMonster_Locomotion')
check(blend.get_editor_property('blend_parameters')[0].get_editor_property('max') == 540, 'Run blend supports increased speed')
run = next(s for s in blend.get_editor_property('sample_data') if s.get_editor_property('animation').get_name() == 'anim_run_1')
check(abs(run.get_editor_property('rate_scale') - 1.5) < .001, 'Run animation matches faster movement')
for name in ['RadicalMike', 'DarkSorcerer']:
    bp = unreal.load_asset(ROOT + name + '/BP_' + name + '_NPC')
    check(reaction(unreal.get_default_object(bp.generated_class()), 'forward_bend_angle') == 42, name + ' retains its original response')
Path(unreal.Paths.project_saved_dir(), 'NPCAggression/Persistence.json').write_text(json.dumps(
    {'passed': True, 'placed': placed, 'checks': checks}, indent=2))
unreal.log('NPC_AGGRESSION_PERSISTENCE_OK ' + str(len(checks)))
