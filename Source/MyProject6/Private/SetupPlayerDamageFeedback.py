"""Import the short player hurt cues as always-resident PCM for immediate feedback."""
import unreal, json
from pathlib import Path
root=Path(unreal.Paths.project_dir()).resolve()
tasks=[]
for index in (1,2,3):
    name=f'S_PlayerHurt_{index:02}'
    task=unreal.AssetImportTask()
    for key,value in {'filename':str(root/'ContentSource/PlayerFeedback'/(name+'.wav')),
        'destination_path':'/Game/Player/Feedback/Audio','destination_name':name,
        'automated':True,'replace_existing':True,'save':True}.items(): task.set_editor_property(key,value)
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
report=[]
for task in tasks:
    paths=task.get_editor_property('imported_object_paths')
    assert len(paths)==1,task.filename
    sound=unreal.load_asset(paths[0])
    sound.set_editor_property('sound_asset_compression_type',unreal.SoundAssetCompressionType.PCM)
    sound.set_editor_property('loading_behavior',unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
    sound.set_editor_property('sound_group',unreal.SoundGroup.SOUNDGROUP_VOICE)
    sound.set_editor_property('looping',False)
    assert unreal.EditorAssetLibrary.save_loaded_asset(sound,False)
    report.append({'asset':sound.get_path_name(),'duration':sound.get_editor_property('duration')})
out=root/'Saved/PlayerFeedback'
out.mkdir(parents=True,exist_ok=True)
(out/'ImportedAudio.json').write_text(json.dumps(report,indent=2))
unreal.log('PLAYER_DAMAGE_FEEDBACK_AUDIO_SAVED')
