"""Import Arc Cage assets, save class defaults and reconcile the main level."""
from pathlib import Path
import json, math, runpy, shutil, unreal
root=Path(unreal.Paths.project_dir()).resolve();out=root/'Saved/ElectricTether';out.mkdir(parents=True,exist_ok=True)
source=root/'ContentSource/ElectricTether';base='/Game/Weapons/ElectricTether'
assets=unreal.AssetToolsHelpers.get_asset_tools();edit=unreal.MaterialEditingLibrary
def create(name,folder,cls,factory):
 return unreal.load_asset(folder+'/'+name) if unreal.EditorAssetLibrary.does_asset_exist(folder+'/'+name) else assets.create_asset(name,folder,cls,factory)
def save(a):assert unreal.EditorAssetLibrary.save_loaded_asset(a,False),a
materials={}
for name,(color,metal,rough,emission) in json.loads((source/'Palette.json').read_text()).items():
 mi=create('MI_'+name,base+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
 edit.set_material_instance_parent(mi,unreal.load_asset('/Game/Weapons/Railgun/Materials/M_RailMetal'))
 edit.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*color,1))
 for k,v in [('Metallic',metal),('Roughness',rough),('Emission',emission)]:edit.set_material_instance_scalar_parameter_value(mi,k,v)
 save(mi);materials[name]=mi
task=unreal.AssetImportTask()
for k,v in {'filename':str(source/'SM_ArcCage.fbx'),'destination_path':base+'/Meshes','destination_name':'SM_ArcCage','automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(k,v)
options=unreal.FbxImportUI()
for k,v in {'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False}.items():options.set_editor_property(k,v)
for k,v in {'combine_meshes':True,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}.items():options.static_mesh_import_data.set_editor_property(k,v)
task.set_editor_property('options',options);assets.import_asset_tasks([task]);mesh=unreal.load_asset(base+'/Meshes/SM_ArcCage');assert mesh
slots=mesh.get_editor_property('static_materials')
for i,s in enumerate(slots):s.set_editor_property('material_interface',materials[str(s.material_slot_name)]);slots[i]=s
mesh.set_editor_property('static_materials',slots);save(mesh)
for name in ['Hum','Catch','Release']:
 task=unreal.AssetImportTask()
 for k,v in {'filename':str(source/('S_Arc'+name+'.wav')),'destination_path':base+'/Audio','destination_name':'S_Arc'+name,'automated':True,'replace_existing':True,'save':True}.items():task.set_editor_property(k,v)
 assets.import_asset_tasks([task]);sound=unreal.load_asset(base+'/Audio/S_Arc'+name);sound.set_editor_property('looping',name=='Hum');save(sound)
runpy.run_path(str(Path(__file__).with_name('UpdateElectricTetherMaterials.py')))
runpy.run_path(str(Path(__file__).with_name('UpdatePickupMotifMaterial.py')))
# A dedicated skeletal idle cycle; the live rig adds load tremor, sway and release recoil.
anim_path=base+'/Animations/A_ArcHold'
if not unreal.EditorAssetLibrary.does_asset_exist(anim_path):unreal.EditorAssetLibrary.duplicate_asset('/Game/Weapons/Railgun/Animations/A_RailgunHold',anim_path)
hold=unreal.load_asset(anim_path);reference=unreal.load_asset('/Game/Weapons/Railgun/Animations/A_RailgunHold')
opts=unreal.AnimPoseEvaluationOptions();opts.set_editor_property('should_retarget',False)
ext=unreal.AnimPoseExtensions;pose=ext.get_anim_pose_at_time(reference,0,opts);names=ext.get_bone_names(pose)
ctrl=hold.controller;ctrl.open_bracket('Arc Cage steady breathing hold',False)
try:
 ctrl.set_frame_rate(unreal.FrameRate(30,1),False);ctrl.set_number_of_frames(unreal.FrameNumber(60),False);ctrl.remove_all_bone_tracks(False)
 for n in names:
  original=ext.get_bone_pose(pose,n,unreal.AnimPoseSpaces.LOCAL);positions=[];rotations=[];scales=[]
  for i in range(61):
   p=original.translation
   if str(n).lower()=='root':p=unreal.Vector(p.x,p.y,p.z+.22*math.sin(i*math.tau/60))
   positions.append(p);rotations.append(original.rotation);scales.append(original.scale3d)
  ctrl.add_bone_curve(n,False);assert ctrl.set_bone_track_keys(n,positions,rotations,scales,False)
finally:ctrl.close_bracket(False)
hold.set_editor_property('enable_root_motion',False);save(hold)
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.ParametricPickup)
pickup_path='/Game/GameplayActors/Pickups/ElectricTether/BP_ArcCagePickup'
bp=create('BP_ArcCagePickup',pickup_path.rsplit('/',1)[0],unreal.Blueprint,factory)
settings={'weapon_reward':unreal.PickupWeapon.ELECTRIC_TETHER,'icon':unreal.PickupMotif.ELECTRIC_TETHER,'shape':unreal.PickupShape.ROUNDED_CUBE,'size':60.,'corner_radius':10.,'body_color':unreal.LinearColor(.015,.02,.045,1),'icon_color':unreal.LinearColor(.22,.7,1,1),'icon_scale':.8,'minimum_glow':1.8,'maximum_glow':4.,'motion':unreal.PickupMotion.SPIN_AND_BOB,'spin_speed':28.,'bob_height':16.,'bob_period':2.4,'auto_collect':True,'player_only':True}
for k,v in settings.items():unreal.get_default_object(bp.generated_class()).set_editor_property(k,v)
unreal.BlueprintEditorLibrary.compile_blueprint(bp);save(bp)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
map_path='/Game/FirstPerson/Lvl_FirstPerson';backup=out/'Before/Lvl_FirstPerson.umap';backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():shutil.copy2(root/'Content/FirstPerson/Lvl_FirstPerson.umap',backup)
assert level.load_level(map_path)
def record(a):
 p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
 return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'folder':str(a.get_folder_path())}
all_actors=actors.get_all_level_actors();pickups=[a for a in all_actors if isinstance(a,unreal.ParametricPickup)]
npcs=[a for a in all_actors if isinstance(a,(unreal.ChasingNPC,unreal.WhisperFollower))]
records=[record(a) for a in npcs+pickups if a.get_actor_label()!='BP_ArcCagePickup']
for npc in npcs:
 c=npc.get_component_by_class(unreal.NPCElectricTetherComponent);assert c,npc.get_actor_label()
 d=unreal.get_default_object(npc.get_class()).get_component_by_class(unreal.NPCElectricTetherComponent);assert d
 for k in ['impact_damage_scale','minimum_impact_speed','impact_cooldown']:c.set_editor_property(k,d.get_editor_property(k))
pickup=next((a for a in pickups if a.get_actor_label()=='BP_ArcCagePickup'),None)
if not pickup:
 ref=next(a for a in pickups if a.get_actor_label()=='BP_SleepSignalPickup');p=ref.get_actor_location();p.y-=150
 while any((a.get_actor_location()-p).length()<110 for a in pickups):p.y-=150
 pickup=actors.spawn_actor_from_class(bp.generated_class(),p,ref.get_actor_rotation());pickup.set_actor_label('BP_ArcCagePickup');pickup.set_folder_path(ref.get_folder_path())
for k,v in settings.items():pickup.set_editor_property(k,v)
pickup.refresh_pickup();assert level.save_current_level()
(out/'Placement.json').write_text(json.dumps({'map':map_path,'existing':records,'pickup':record(pickup),'pickup_blueprint':pickup_path},indent=2))
unreal.log('ELECTRIC_TETHER_SETUP_OK')
