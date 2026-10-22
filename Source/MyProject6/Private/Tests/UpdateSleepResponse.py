"""Persist sleep recovery tuning on NPC classes and reconcile placed overrides only."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'SleepSignal'
data=json.loads((out/'Placement.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
# Explicitly retain masking on fade instances copied from an opaque source shader.
for index in range(2):
    instance=unreal.load_asset('/Game/Materials/NPC/MI_NPCDeath_Slot'+str(index))
    overrides=instance.get_editor_property('base_property_overrides')
    overrides.set_editor_property('override_blend_mode',True)
    overrides.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    instance.set_editor_property('base_property_overrides',overrides)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    assert unreal.EditorAssetLibrary.save_loaded_asset(instance,False)
updated=[]
for path in unreal.EditorAssetLibrary.list_assets('/Game/NPC',True,False):
    if '/BP_' not in path:continue
    bp=unreal.load_asset(path)
    if not isinstance(bp,unreal.Blueprint):continue
    cdo=unreal.get_default_object(bp.generated_class())
    if not isinstance(cdo,(unreal.ChasingNPC,unreal.WhisperFollower)):continue
    sleep=cdo.get_component_by_class(unreal.NPCSleepComponent)
    assert sleep,path
    sleep.set_editor_property('wake_duration',2.9)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.get_default_object(bp.generated_class()).get_component_by_class(unreal.NPCSleepComponent).set_editor_property('wake_duration',2.9)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    updated.append(path)
assert level.load_level(data['map'])
def record(a):
    p,r,s=a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d()
    return {'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'pose':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'folder':str(a.get_folder_path())}
by_label={a.get_actor_label():a for a in actors.get_all_level_actors()}
for row in data['npcs']:
    actor=by_label[row['label']]
    assert record(actor)==row
    component=actor.get_component_by_class(unreal.NPCSleepComponent)
    default=unreal.get_default_object(actor.get_class()).get_component_by_class(unreal.NPCSleepComponent)
    for key in ['drowsy_duration','fall_duration','wake_duration']:
        component.set_editor_property(key,default.get_editor_property(key))
    assert record(actor)==row
assert level.save_current_level()
(out/'ResponseMigration.json').write_text(json.dumps({'classes':updated,'placed_npcs':len(data['npcs']),'wake_duration':2.9},indent=2))
unreal.log('SLEEP_RESPONSE_MIGRATION_OK')
