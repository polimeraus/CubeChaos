"""Save reusable bomb pickup defaults and add one pickup to the current project's weapon row."""
from pathlib import Path
import json, shutil, unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/FuseBomb';out.mkdir(parents=True,exist_ok=True)
map_path='/Game/FirstPerson/Lvl_FirstPerson'
source=root/'Content/FirstPerson/Lvl_FirstPerson.umap'
backup=out/'Before/Lvl_FirstPerson.umap';backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():shutil.copy2(source,backup)
assets=unreal.AssetToolsHelpers.get_asset_tools()
path='/Game/GameplayActors/Pickups/FuseBomb/BP_BombPickup'
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ParametricPickup)
bp=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('BP_BombPickup',str(Path(path).parent).replace('\\','/'),unreal.Blueprint,factory)
cdo=unreal.get_default_object(bp.generated_class())
settings={'weapon_reward':unreal.PickupWeapon.BOMB,'icon':unreal.PickupMotif.BOMB,
          'shape':unreal.PickupShape.ROUNDED_CUBE,'size':60.,'corner_radius':8.,
          'body_color':unreal.LinearColor(.035,.023,.017,1),'icon_color':unreal.LinearColor(1,.28,.035,1),
          'icon_scale':.76,'minimum_glow':1.8,'maximum_glow':4.,'motion':unreal.PickupMotion.SPIN_AND_BOB,
          'spin_speed':35.,'bob_height':18.,'bob_period':2.4,'auto_collect':True,'player_only':True}
for key,value in settings.items():cdo.set_editor_property(key,value)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(map_path)
pickups=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricPickup)]

def record(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),
            'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],
            'reward':str(a.weapon_reward),'icon':str(a.icon),'folder':str(a.get_folder_path())}

existing=[record(a) for a in pickups if a.get_actor_label()!='BP_BombPickup']
bomb=next((a for a in pickups if a.get_actor_label()=='BP_BombPickup'),None)
if bomb is None:
    reference=next(a for a in pickups if a.get_actor_label()=='BP_ParametricPickup4')
    row=[a for a in pickups if abs(a.get_actor_location().x-reference.get_actor_location().x)<10]
    location=reference.get_actor_location();location.y=min(a.get_actor_location().y for a in row)-150
    bomb=actors.spawn_actor_from_class(bp.generated_class(),location,reference.get_actor_rotation())
    bomb.set_actor_label('BP_BombPickup');bomb.set_folder_path(reference.get_folder_path())
for key,value in settings.items():bomb.set_editor_property(key,value)
bomb.refresh_pickup()
assert [record(a) for a in pickups if a.get_actor_label()!='BP_BombPickup']==existing
assert level.save_current_level()
(out/'Placement.json').write_text(json.dumps({'map':map_path,'blueprint':path,'bomb':record(bomb),'existing':existing},indent=2))
unreal.log('BOMB_PICKUP_SAVED '+str(bomb.get_actor_location()))
