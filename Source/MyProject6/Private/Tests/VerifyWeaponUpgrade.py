"""Fresh-process assertions against saved level actors, Blueprints and shader asset."""
from pathlib import Path
import json, unreal
out=Path(unreal.Paths.project_saved_dir())/'WeaponUpgrade'
data=json.loads((out/'SavedSettings.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]
native=unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower)
bp=unreal.load_asset(data['tower_blueprint'])
assert native.entrance_height==200 and unreal.get_default_object(bp.generated_class()).entrance_height==200
assert unreal.get_default_object(unreal.load_asset(data['default_pickup']).generated_class()).weapon_reward==unreal.PickupWeapon.DEFAULT_WEAPON
for map_path in sorted(set(r['map'] for r in data['towers'])):
    assert unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    all_actors=actors.get_all_level_actors()
    for r in [r for r in data['towers'] if r['map']==map_path]:
        a=next(a for a in all_actors if a.get_actor_label()==r['label'])
        assert a.entrance_height==r['height'] and pose(a)==r['pose'] and str(a.get_folder_path())==r['folder']
        assert not a.generation_warning
        fresh=actors.spawn_actor_from_class(a.get_class(),unreal.Vector(25000,0,0))
        assert fresh.entrance_height==200 and fresh.generated_piece_count>0 and not fresh.generation_warning
        actors.destroy_actor(fresh)
    if map_path=='/Game/FirstPerson/Lvl_FirstPerson':
        pickups={a.get_actor_label():a for a in all_actors if isinstance(a,unreal.ParametricPickup)}
        for r in data['pickups']:
            a=pickups[r['label']];assert str(a.weapon_reward)==r['reward'] and pose(a)==r['pose']
        assert pickups['BP_ParametricPickup4'].weapon_reward==unreal.PickupWeapon.BFG
        assert pickups['BP_ParametricPickup'].weapon_reward==unreal.PickupWeapon.RAILGUN
        assert pickups['BP_ParametricPickup2'].weapon_reward==unreal.PickupWeapon.DEFAULT_WEAPON
m=unreal.load_asset('/Game/Weapons/BFG/Materials/M_BFGPlasma')
shader=next(n for n in unreal.MaterialEditingLibrary.get_material_expressions(m) if isinstance(n,unreal.MaterialExpressionCustom))
assert shader.get_editor_property('code')==Path(__file__).parents[1].joinpath('BFGPlasma.ush').read_text()
(out/'Persistence.json').write_text(json.dumps({'fresh_process_verified':True,**data},indent=2))
unreal.log('WEAPON_UPGRADE_PERSISTENCE_OK')
