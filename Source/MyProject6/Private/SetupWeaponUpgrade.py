"""Persist only requested weapon rewards, plasma shader and Babylon entrance height."""
from pathlib import Path
from datetime import datetime
import json, shutil, unreal

root=Path(unreal.Paths.project_dir()).resolve()
content=root/'Content'
out=root/'Saved/WeaponUpgrade';out.mkdir(parents=True,exist_ok=True)
backup=out/'Backups'/datetime.now().strftime('%Y%m%d_%H%M%S')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
edit=unreal.MaterialEditingLibrary

def protect(path,extension='.uasset'):
    file=content/(path.split('.')[0].removeprefix('/Game/')+extension)
    if file.exists():
        dest=backup/file.relative_to(content);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)

def pose(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return [p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z]

material_path='/Game/Weapons/BFG/Materials/M_BFGPlasma'
protect(material_path)
material=unreal.load_asset(material_path);assert material
shader=next(n for n in edit.get_material_expressions(material) if isinstance(n,unreal.MaterialExpressionCustom))
shader.set_editor_property('code',Path(__file__).with_name('BFGPlasma.ush').read_text())
edit.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material,False)

tower_path='/Game/Structures/Destructible/BabylonTower/BP_ChunkedDestructibleBabylonTower'
protect(tower_path)
bp=unreal.load_asset(tower_path);assert bp
cdo=unreal.get_default_object(bp.generated_class())
assert unreal.get_default_object(unreal.ChunkedDestructibleBabylonTower).entrance_height==200
cdo.set_editor_property('entrance_height',200.)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

# Reusable pickup class for the original weapon; preserve existing enum ordinals.
default_path='/Game/GameplayActors/Pickups/DefaultWeapon/BP_DefaultWeaponPickup'
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ParametricPickup)
default_bp=unreal.load_asset(default_path) if unreal.EditorAssetLibrary.does_asset_exist(default_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_DefaultWeaponPickup',default_path.rsplit('/',1)[0],unreal.Blueprint,factory)
default_settings={'weapon_reward':unreal.PickupWeapon.DEFAULT_WEAPON,'icon':unreal.PickupMotif.ROCKET,'icon_color':unreal.LinearColor(1,.48,.045,1)}
for key,value in default_settings.items():unreal.get_default_object(default_bp.generated_class()).set_editor_property(key,value)
unreal.BlueprintEditorLibrary.compile_blueprint(default_bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(default_bp,False)

records=[]
maps=['/Game/FirstPerson/Lvl_FirstPerson','/Game/Structures/Destructible/BabylonTower/Maps/Lvl_BabylonTowerPreview']
for map_path in maps:
    protect(map_path,'.umap');assert level.load_level(map_path)
    all_actors=actors.get_all_level_actors()
    for a in all_actors:
        if not isinstance(a,unreal.ChunkedDestructibleBabylonTower):continue
        old_pose=pose(a);label=a.get_actor_label();folder=str(a.get_folder_path())
        # Larger authored entrances remain larger; stale 185 cm overrides are reconciled.
        a.set_editor_property('entrance_height',max(200.,a.entrance_height));a.rebuild_tower()
        assert pose(a)==old_pose and a.get_actor_label()==label and str(a.get_folder_path())==folder
        records.append({'map':map_path,'label':label,'class':a.get_class().get_path_name(),'pose':old_pose,'folder':folder,'height':a.entrance_height})
    if map_path==maps[0]:
        pickups={a.get_actor_label():a for a in all_actors if isinstance(a,unreal.ParametricPickup)}
        assert {'BP_ParametricPickup','BP_ParametricPickup2','BP_ParametricPickup4'}<=set(pickups)
        rewards={'BP_ParametricPickup':{'weapon_reward':unreal.PickupWeapon.RAILGUN},
                 'BP_ParametricPickup2':default_settings,
                 'BP_ParametricPickup4':{'weapon_reward':unreal.PickupWeapon.BFG,'icon':unreal.PickupMotif.RADIATION_BOMB,'icon_color':unreal.LinearColor(.03,1,.12,1)}}
        for label,settings in rewards.items():
            a=pickups[label];old_pose=pose(a)
            for key,value in settings.items():a.set_editor_property(key,value)
            a.refresh_pickup();assert pose(a)==old_pose
        pickup_records=[{'label':a.get_actor_label(),'reward':str(a.weapon_reward),'pose':pose(a)} for a in pickups.values()]
    assert level.save_current_level()
(out/'SavedSettings.json').write_text(json.dumps({'towers':records,'pickups':pickup_records,'tower_blueprint':tower_path,'default_pickup':default_path},indent=2))
unreal.log('WEAPON_UPGRADE_SAVED')
