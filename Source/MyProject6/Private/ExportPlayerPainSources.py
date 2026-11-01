"""Export existing project-owned human effort recordings for a short player hurt cue."""
import unreal, json
from pathlib import Path
root = Path(unreal.Paths.project_dir()).resolve()
out = root/'ContentSource/PlayerFeedback/Original'
out.mkdir(parents=True,exist_ok=True)
report=[]
for index in (1,2,3):
    name=f'FengMao_Effort_Pain_{index:02}'
    path='/Game/NPC/ParagonFengMao/Characters/Heroes/FengMao/Sounds/SoundWaves/'+name
    sound=unreal.load_asset(path)
    assert sound, path
    task=unreal.AssetExportTask()
    task.object=sound
    task.filename=str(out/(name+'.wav'))
    task.exporter=unreal.SoundExporterWAV()
    task.automated=True
    task.prompt=False
    task.replace_identical=True
    assert unreal.Exporter.run_asset_export_task(task)
    report.append({'asset':path,'duration':sound.get_editor_property('duration'),'file':task.filename})
(out/'Sources.json').write_text(json.dumps(report,indent=2))
unreal.log('PLAYER_PAIN_SOURCES_EXPORTED')
