from pathlib import Path
import json, unreal
out=Path(unreal.Paths.project_saved_dir()).resolve()/'BombPolish';out.mkdir(parents=True,exist_ok=True)
texbase='/Game/NPC/ParagonGRIMexe/FX/Textures/Explosions/'
names=['T_FireBall_Fluid_07_E','T_FireBall_Fluid_07_MeshedDepth_8X8_PreMult','T_FireBall_07C_ResdiualSmokey','T_Explosion_Test_v1','HyperBreach/T_FireBall_07D_E']
report={'textures':[],'api':[n for n in dir(unreal.AnimPoseExtensions) if 'bone' in n or 'ref' in n], 'mesh_api':[n for n in dir(unreal.SkeletalMesh) if 'bone' in n or 'ref_pose' in n]}
for name in names:
    tex=unreal.load_asset(texbase+name);assert tex
    task=unreal.AssetExportTask();task.object=tex;task.filename=str(out/(name.split('/')[-1]+'.png'))
    task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.TextureExporterPNG()
    report['textures'].append({'path':tex.get_path_name(),'exported':unreal.Exporter.run_asset_export_task(task),'srgb':tex.srgb,'compression':str(tex.compression_settings)})
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
player=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),unreal.Vector())
gun=actors.spawn_actor_from_class(unreal.RailgunWeapon,unreal.Vector());gun.equip(player)
frame=gun.arms.get_relative_transform()
report['arms_frame']=str(frame)
report['camera']=str(player.get_component_by_class(unreal.CameraComponent).get_editor_property('first_person_field_of_view'))
options=unreal.AnimPoseEvaluationOptions();options.optional_skeletal_mesh=gun.arms.get_skeletal_mesh_asset()
ext=unreal.AnimPoseExtensions
def vec(v):return [v.x,v.y,v.z]
def transform(t):return {'p':vec(t.translation),'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w]}
for name,path in [('rifle','/Game/Weapons/Railgun/Animations/A_RailgunHold'),('bomb','/Game/Weapons/FuseBomb/Animations/A_BombHold')]:
    pose=ext.get_anim_pose_at_time(unreal.load_asset(path),0,options)
    rows={}
    for bone in ext.get_bone_names(pose):
        bone=str(bone)
        if any(n in bone for n in ['hand','arm','thumb','index','middle','ring','pinky']):
            w=ext.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
            rows[bone]={'local':transform(ext.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.LOCAL)),
                        'world':transform(w),'rig':vec(unreal.MathLibrary.transform_location(frame,w.translation))}
    report[name]=rows
(out/'Inspection.json').write_text(json.dumps(report,indent=2))
unreal.log('BOMB_PRESENTATION_INSPECTED')
