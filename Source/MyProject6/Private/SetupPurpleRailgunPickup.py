"""Save reusable purple railgun defaults and extend the existing pickup row once."""
from pathlib import Path
import json, runpy, unreal

root=Path(unreal.Paths.project_dir()).resolve();out=root/'Saved/PurpleRailgun'
assets=unreal.AssetToolsHelpers.get_asset_tools();lib=unreal.EditorAssetLibrary
runpy.run_path(str(Path(__file__).with_name('UpdatePickupMotifMaterial.py')),run_name='__main__')
path='/Game/GameplayActors/Pickups/PurpleRailgun/BP_PurpleRailgunPickup'
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ParametricPickup)
bp=unreal.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('BP_PurpleRailgunPickup',path.rsplit('/',1)[0],unreal.Blueprint,factory)
cdo=unreal.get_default_object(bp.generated_class())
settings={'weapon_reward':unreal.PickupWeapon.PURPLE_RAILGUN,'icon':unreal.PickupMotif.PURPLE_RAILGUN,
    'shape':unreal.PickupShape.ROUNDED_CUBE,'size':60.,'corner_radius':8.,
    'body_color':unreal.LinearColor(.025,.012,.04,1),'icon_color':unreal.LinearColor(.8,.015,1,1),
    'icon_scale':.83,'minimum_glow':1.8,'maximum_glow':4.,'pulse_period':2.4,
    'motion':unreal.PickupMotion.SPIN_AND_BOB,'spin_speed':35.,'bob_height':18.,'bob_period':2.4,
    'auto_collect':True,'player_only':True,
    'surface_material':unreal.load_asset('/Game/GameplayActors/Pickups/Shared/Materials/M_PickupSurface')}
for k,v in settings.items():cdo.set_editor_property(k,v)
# Reuse the project's established pickup sound.
existing_cdo=unreal.get_default_object(unreal.load_class(None,'/Game/GameplayActors/Pickups/ParametricPickup/BP_ParametricPickup.BP_ParametricPickup_C'))
cdo.collection_sound=existing_cdo.collection_sound
unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert lib.save_loaded_asset(bp,False)
player_bp=unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
player_defaults=unreal.get_default_object(player_bp.generated_class())
visuals=unreal.RailgunVisualSettings();visuals.color=unreal.LinearColor(.8,.015,1,1);visuals.spiral_color=unreal.LinearColor(.45,.035,1,1)
player_defaults.set_editor_property('purple_railgun_visuals',visuals)
unreal.BlueprintEditorLibrary.compile_blueprint(player_bp);assert lib.save_loaded_asset(player_bp,False)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
pickups=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ParametricPickup)]
def record(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'reward':str(a.weapon_reward),
            'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'folder':str(a.get_folder_path())}
before=[record(a) for a in pickups if a.get_class()!=bp.generated_class()]
new=[a for a in pickups if a.get_class()==bp.generated_class()]
assert len(new)<=1,'Unexpected duplicate purple pickups'
if new:pickup=new[0]
else:
    reference=next(a for a in pickups if a.get_actor_label()=='BP_ParametricPickup4')
    row=[a for a in pickups if abs(a.get_actor_location().x-reference.get_actor_location().x)<10]
    position=reference.get_actor_location();position.y=min(a.get_actor_location().y for a in row)-150.
    pickup=actors.spawn_actor_from_class(bp.generated_class(),position,reference.get_actor_rotation())
    pickup.set_actor_label('BP_PurpleRailgunPickup');pickup.set_folder_path(reference.get_folder_path())
for k,v in settings.items():pickup.set_editor_property(k,v)
pickup.collection_sound=cdo.collection_sound;pickup.refresh_pickup()
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.MyProject6Character):a.set_editor_property('purple_railgun_visuals',visuals)
assert [record(a) for a in pickups if a.get_class()!=bp.generated_class()]==before
assert level.save_current_level()
(out/'Placement.json').write_text(json.dumps({'pickup':record(pickup),'existing':before},indent=2))
unreal.log('PURPLE_RAILGUN_PICKUP_SAVED '+str(pickup.get_actor_location()))
