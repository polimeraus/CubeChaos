"""Rebuild only saved houses that already use Paint Splatter; preserve their settings."""
from pathlib import Path
import json,unreal
out=Path(unreal.Paths.project_saved_dir())/'PaintSplatter';out.mkdir(parents=True,exist_ok=True)
apply='-ApplyPaintUpdate' in unreal.SystemLibrary.get_command_line()
verify='-VerifyPaintUpdate' in unreal.SystemLibrary.get_command_line()
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report=[]
for actor in actors.get_all_level_actors():
    if not isinstance(actor,unreal.SimpleHouseActor):continue
    floors=[]
    for name in ['front_facade','back_facade','side_wall1_facade','side_wall2_facade']:
        facade=actor.get_editor_property(name)
        for index,window in enumerate(facade.get_editor_property('floor_windows')):
            if window.get_editor_property('window_border_style')==unreal.SimpleHouseWindowBorderStyle.PAINT_SPLATTER:
                floors.append({'facade':name,'floor':index+1,'enabled':bool(facade.enable_openings and window.has_windows),
                    'spread':window.paint_spread,'amount':window.paint_splatter_amount,'seed':window.paint_splatter_seed})
    if not floors:continue
    def geometry():
        return sum(c.get_dynamic_mesh().get_triangle_count() for c in actor.get_components_by_class(unreal.DynamicMeshComponent)
                   if c.component_has_tag('PaintSplatter'))
    old=geometry()
    if apply:actor.modify()
    if apply or verify:actor.rebuild_mesh()
    new=geometry()
    if verify:assert old==new, (actor.get_actor_label(),old,new)
    report.append({'label':actor.get_actor_label(),'class':actor.get_class().get_path_name(),'floors':floors,'before_triangles':old,'triangles':new})
if apply and report:
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
(out/('Persistence.json' if verify else 'Updated.json' if apply else 'Inventory.json')).write_text(json.dumps(report,indent=2))
unreal.log('PAINT_'+('PERSISTENCE' if verify else 'UPDATE' if apply else 'INVENTORY')+'_OK '+str(len(report)))
