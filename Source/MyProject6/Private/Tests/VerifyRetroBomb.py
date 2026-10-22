"""Load saved assets in a new editor; verify native inheritance and mannequin anatomy."""
from pathlib import Path
import json, runpy, unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'BombRetro'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
placed=[]
classes=set()
for actor in actors.get_all_level_actors():
    if not isinstance(actor,unreal.Character):continue
    reaction=actor.get_component_by_class(unreal.NPCHitReactionComponent)
    if not reaction:continue
    assert reaction.enable_blast_tumble and reaction.blast_tumble_speed==285.,actor.get_actor_label()
    classes.add(actor.get_class().get_path_name())
    placed.append({'label':actor.get_actor_label(),'class':actor.get_class().get_path_name(),
                   'transform':str(actor.get_actor_transform()),'folder':str(actor.get_folder_path())})
root=Path(unreal.Paths.project_content_dir()).resolve()
for path in (root/'NPC').rglob('BP_*.uasset'):
    if 'NPC' not in path.stem and 'Follower' not in path.stem:continue
    asset='/Game/'+path.relative_to(root).with_suffix('').as_posix()
    classes.add(asset+'.'+path.stem+'_C')
verified=[]
for path in sorted(classes):
    cls=unreal.load_class(None,path)
    cdo=unreal.get_default_object(cls)
    reaction=cdo.get_component_by_class(unreal.NPCHitReactionComponent)
    assert reaction and reaction.enable_blast_tumble and reaction.blast_tumble_speed==285.,path
    fresh=actors.spawn_actor_from_class(cls,unreal.Vector(40000,0,5000))
    r=fresh.get_component_by_class(unreal.NPCHitReactionComponent)
    assert r.enable_blast_tumble and r.blast_tumble_speed==285.
    actors.destroy_actor(fresh)
    verified.append(path)
assert len(verified)>=23
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),unreal.Vector())
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector())
gun.equip(player);gun.activate_bomb()
options=unreal.AnimPoseEvaluationOptions()
options.optional_skeletal_mesh=gun.arms.get_skeletal_mesh_asset()
options.should_retarget=False
ext=unreal.AnimPoseExtensions
anim=unreal.load_asset('/Game/Weapons/FuseBomb/Animations/A_BombHold')
checked=0
for t in [0.,.25,.5]:
    pose=ext.get_anim_pose_at_time(anim,t,options)
    for name in ext.get_bone_names(pose):
        if not str(name).startswith(('thumb_','index_','middle_','ring_','pinky_')):continue
        actual=ext.get_bone_pose(pose,name,unreal.AnimPoseSpaces.LOCAL)
        ref=ext.get_ref_bone_pose(pose,name,unreal.AnimPoseSpaces.LOCAL)
        assert (actual.translation-ref.translation).length()<.01,str(name)
        assert (actual.scale3d-ref.scale3d).length()<.001,str(name)
        checked+=1
mesh=unreal.load_asset('/Game/Weapons/FuseBomb/Meshes/SM_FuseBomb')
slots=[str(s.material_slot_name) for s in mesh.static_materials]
assert 'BombCyan' in slots and 'BombMagenta' in slots
(out/'Persistence.json').write_text(json.dumps({'fresh_process':True,'placed_npcs':placed,
    'verified_classes':verified,'finger_checks':checked,'mannequin_mesh':options.optional_skeletal_mesh.get_path_name(),'bomb_material_slots':slots},indent=2))
unreal.log('RETRO_BOMB_PERSISTENCE_OK')
runpy.run_path(str(Path(__file__).with_name('VerifyFuseBomb.py')))
