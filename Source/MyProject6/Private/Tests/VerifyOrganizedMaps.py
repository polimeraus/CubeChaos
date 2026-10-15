"""Reload each authored preview map from its organized, saved package."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'ContentOrganization'
plan=json.loads((out/'Plan.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rows=[]
for row in plan['moves']:
    if row['type']!='World' or not row['target'].startswith(('/Game/Structures/','/Game/GameplayActors/')):continue
    world=unreal.EditorLoadingAndSavingUtils.load_map(row['target'])
    assert world,row
    count=len(actors.get_all_level_actors())
    assert count>0,row
    rows.append(dict(package=row['target'],actors=count))
assert len(rows)==6,len(rows)
(out/'MapVerification.json').write_text(json.dumps(dict(passed=True,maps=rows),indent=2))
unreal.log('ORGANIZED_MAPS_VERIFIED '+str(len(rows)))
