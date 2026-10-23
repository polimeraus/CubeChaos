"""Fresh-process proof of saved assets, NPC inheritance and unchanged level-specific values."""
import json,unreal
from pathlib import Path
out=Path(unreal.Paths.project_saved_dir()).resolve()/'ElectricTether';data=json.loads((out/'Placement.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level(data['map']);by_label={a.get_actor_label():a for a in actors.get_all_level_actors()}
def record(a):
 p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
 return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'folder':str(a.get_folder_path())}
for row in data['existing']+[data['pickup']]:assert record(by_label[row['label']])==row,row
pickup=by_label['BP_ArcCagePickup'];bp=unreal.load_asset(data['pickup_blueprint']);default=unreal.get_default_object(bp.generated_class())
for item in [pickup,default]:
 assert item.weapon_reward==unreal.PickupWeapon.ELECTRIC_TETHER
 assert item.icon==unreal.PickupMotif.ELECTRIC_TETHER
classes={a.get_class() for a in by_label.values() if isinstance(a,(unreal.ChasingNPC,unreal.WhisperFollower))}
for path in unreal.EditorAssetLibrary.list_assets('/Game/NPC',True,False):
 if '/BP_' not in path:continue
 asset=unreal.load_asset(path)
 if not isinstance(asset,unreal.Blueprint):continue
 cdo=unreal.get_default_object(asset.generated_class())
 if isinstance(cdo,(unreal.ChasingNPC,unreal.WhisperFollower)):classes.add(asset.generated_class())
for cls in classes:
 d=unreal.get_default_object(cls).get_component_by_class(unreal.NPCElectricTetherComponent);assert d,cls
 fresh=actors.spawn_actor_from_class(cls,unreal.Vector(25000,0,1000));component=fresh.get_component_by_class(unreal.NPCElectricTetherComponent);assert component
 for key in ['impact_damage_scale','minimum_impact_speed','impact_cooldown']:assert component.get_editor_property(key)==d.get_editor_property(key)
 actors.destroy_actor(fresh)
for a in by_label.values():
 if not isinstance(a,(unreal.ChasingNPC,unreal.WhisperFollower)):continue
 c=a.get_component_by_class(unreal.NPCElectricTetherComponent);d=unreal.get_default_object(a.get_class()).get_component_by_class(unreal.NPCElectricTetherComponent)
 assert c and d,a.get_actor_label()
 for key in ['impact_damage_scale','minimum_impact_speed','impact_cooldown']:assert c.get_editor_property(key)==d.get_editor_property(key)
for name in ['Meshes/SM_ArcCage','Materials/M_ArcField','Materials/M_ArcEnvelope','Animations/A_ArcHold','Audio/S_ArcHum','Audio/S_ArcCatch','Audio/S_ArcRelease']:assert unreal.load_asset('/Game/Weapons/ElectricTether/'+name)
membrane=unreal.load_asset('/Game/Weapons/ElectricTether/Materials/M_ArcEnvelope')
assert membrane.get_editor_property('blend_mode')==unreal.BlendMode.BLEND_TRANSLUCENT
(out/'Persistence.json').write_text(json.dumps({'verified':True,'npc_classes':sorted(c.get_path_name() for c in classes),'preserved_level_actors':len(data['existing']),'pickup':record(pickup)},indent=2))
unreal.log('ELECTRIC_TETHER_PERSISTENCE_OK classes='+str(len(classes)))
