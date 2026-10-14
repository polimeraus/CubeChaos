"""Persist the new shared defaults and reconcile existing Ziggurats without moving them."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir())
content=Path(unreal.Paths.project_content_dir())
out=Path(unreal.Paths.project_saved_dir())/'ZigguratTraversal'
backup=out/'Backups'/datetime.now().strftime('%Y%m%d_%H%M%S')
backup.mkdir(parents=True,exist_ok=True)
paths=['/Game/Structures/Ziggurat/BP_DestructibleZiggurat',
       '/Game/Structures/ZigguratPyramit/BP_ChunkedDestructibleZigguratPyramit']
records=[]
for path in paths:
    source=content/(path.removeprefix('/Game/')+'.uasset')
    shutil.copy2(source,backup/source.name)
    bp=unreal.load_asset(path)
    defaults=unreal.get_default_object(bp.generated_class())
    settings=defaults.get_editor_property('traversal')
    settings.set_editor_property('floor_openings',True)
    settings.set_editor_property('ground_launch_pad',True)
    assert settings.get_editor_property('arrows_material') and settings.get_editor_property('rings_material')
    defaults.set_editor_property('traversal',settings)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)

# Package name tables contain the native class / Blueprint references, including
# placed native actors. Only maps actually containing one of these classes are saved.
for source in sorted(content.rglob('*.umap')):
    data=source.read_bytes()
    if b'ChunkedDestructibleZiggurat' not in data and b'BP_DestructibleZiggurat' not in data:
        continue
    path='/Game/'+source.relative_to(content).with_suffix('').as_posix()
    shutil.copy2(source,backup/source.name)
    world=unreal.EditorLoadingAndSavingUtils.load_map(path)
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    temples=[a for a in subsystem.get_all_level_actors() if isinstance(a,(unreal.ChunkedDestructibleZiggurat,unreal.ChunkedDestructibleZigguratPyramit))]
    for actor in temples:
        transform=actor.get_actor_transform()
        label=actor.get_actor_label()
        folder=actor.get_folder_path()
        settings=actor.get_editor_property('traversal')
        settings.set_editor_property('floor_openings',True)
        settings.set_editor_property('ground_launch_pad',True)
        actor.set_editor_property('traversal',settings)
        if isinstance(actor,unreal.ChunkedDestructibleZiggurat):actor.rebuild_ziggurat()
        else:actor.rebuild_ziggurat_pyramit()
        assert not actor.generation_warning
        assert actor.get_actor_transform()==transform and actor.get_actor_label()==label and actor.get_folder_path()==folder
        pad=actor.get_editor_property('ground_launch_pad').get_editor_property('child_actor')
        assert isinstance(pad,unreal.LaunchPad) and pad.get_editor_property('use_target_height')
        records.append({'map':path,'label':label,'class':actor.get_class().get_path_name(),
                        'opening_width':actor.effective_opening_width,'pieces':actor.generated_piece_count,
                        'height':actor.generated_height,'pad_material':pad.pad_material.get_path_name()})
    if temples:assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
(out/'SavedInstances.json').write_text(json.dumps(records,indent=2))
unreal.log('ZIGGURAT_TRAVERSAL_SAVED '+json.dumps(records))
