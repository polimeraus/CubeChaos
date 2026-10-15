"""Execute the reviewed package manifest through Unreal's AssetTools rename API.

The external rollback archive must exist before running. Does not copy level
instances into class defaults or replace existing actors.
"""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()).resolve() / 'ContentOrganization'
assert (out/'BackupHashes.json').exists(), 'Verified rollback archive is required'
plan = json.loads((out/'Plan.json').read_text())
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
tools = unreal.AssetToolsHelpers.get_asset_tools()
recovered_path=out/'RecoveredBatch.json'
if recovered_path.exists() and not (out/'RecoveredBatchVerified.json').exists():
    recovered=json.loads(recovered_path.read_text())
    for row in recovered:
        asset=unreal.load_asset(row['target'])
        assert asset, row
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False), row
    (out/'RecoveredBatchVerified.json').write_text(json.dumps(dict(loaded_and_saved=len(recovered))))
    unreal.SystemLibrary.collect_garbage()
journal_path = out/'RenameJournal.json'
journal = json.loads(journal_path.read_text()) if journal_path.exists() else []
done = {r['source'] for r in journal}
for row in plan['moves']:
    if row['source'] in done or row['type'] in ('Rig','ObjectRedirector','World'): continue
    data = registry.get_assets_by_package_name(row['target'])
    if data and any(str(a.asset_class_path.asset_name) == row['type'] for a in data):
        journal.append(row)
        done.add(row['source'])
pending = [r for r in plan['moves'] if r['source'] not in done and r['type'] not in ('Rig','ObjectRedirector','World')]

# World assets are renamed last, after the assets they reference have moved.
pending.sort(key=lambda r: (r['type'] == 'World', r['source']))
for start in range(0, len(pending), 40):
    rows = pending[start:start+40]
    moves = []
    for row in rows:
        asset = unreal.load_asset(row['source'])
        assert asset, 'Cannot load ' + row['source']
        assert asset.get_path_name().split('.')[0] == row['source'], 'Unexpected redirect ' + row['source']
        folder, name = row['target'].rsplit('/',1)
        moves.append(unreal.AssetRenameData(asset=asset, new_package_path=folder, new_name=name))
    assert unreal.ContentOrganizationEditorLibrary.rename_organized_assets(
        [m.asset for m in moves], [r['target'] for r in rows]), 'Rename batch failed ' + str(start)
    for row, move in zip(rows,moves):
        assert move.asset.get_path_name().split('.')[0] == row['target'], row
        if row['type'] != 'World':
            assert unreal.EditorAssetLibrary.save_loaded_asset(move.asset, True), row
    journal.extend(rows)
    journal_path.write_text(json.dumps(journal,indent=2))
    unreal.log('CONTENT_RENAME_PROGRESS %d/%d' % (len(journal),len(plan['moves'])))
    moves.clear()
    asset = None
    move = None
    unreal.SystemLibrary.collect_garbage()

created = []
for row in plan['create']:
    path = row['package']
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        bp = unreal.load_asset(path)
    else:
        parent = getattr(unreal, row['parent'])
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', parent)
        folder, name = path.rsplit('/',1)
        bp = tools.create_asset(name, folder, unreal.Blueprint, factory)
        assert bp, path
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False), path
    created.append(path)
(out/'CreatedBlueprints.json').write_text(json.dumps(created,indent=2))
unreal.log('CONTENT_ORGANIZATION_RENAME_OK moved=%d created=%d' % (len(journal),len(created)))
