"""Persist the open windows / optional tread-only staircase in the Blueprint and placed actors.

Run with the editor closed after compiling. Preserve every existing layout setting
and actor transform. Unrelated external actor packages are verified byte-for-byte.
"""
import hashlib
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal

content = Path(unreal.Paths.project_content_dir()).resolve()
out = Path(unreal.Paths.project_saved_dir()).resolve() / 'FantasyHouse'
out.mkdir(parents=True, exist_ok=True)
backup = out / ('Backup_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
external = content / '__ExternalActors__/FirstPerson/Lvl_FirstPerson'
files = list(external.rglob('*.uasset')) + [
    content / 'FirstPerson/Lvl_FirstPerson.umap',
    content / 'Structures/DynamicMesh/FantasyHouse/Maps/Lvl_FantasyHousePreview.umap',
    content / 'Structures/DynamicMesh/FantasyHouse/BP_ParametricFantasyHouse.uasset']
before = {}
for path in files:
    if not path.exists(): continue
    relative = path.relative_to(content)
    before[str(relative)] = hashlib.sha256(path.read_bytes()).hexdigest()
    destination = backup / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, destination)

bp = unreal.load_asset('/Game/Structures/DynamicMesh/FantasyHouse/BP_ParametricFantasyHouse')
assert bp
cdo = unreal.get_default_object(bp.generated_class())
cdo.modify()
# Preserve the existing staircase dimensions and enabled state. Removed handrail /
# column properties are ignored on load and disappear from the re-saved assets.
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = []
for level in ('/Game/Structures/DynamicMesh/FantasyHouse/Maps/Lvl_FantasyHousePreview', '/Game/FirstPerson/Lvl_FirstPerson'):
    world = unreal.EditorLoadingAndSavingUtils.load_map(level)
    assert world
    houses = [a for a in subsystem.get_all_level_actors() if isinstance(a, unreal.ParametricFantasyHouse)]
    if not houses: continue
    for house in houses:
        transform = house.get_actor_transform()
        house.modify()
        house.house_mesh.modify()
        house.rebuild_house()
        assert house.get_actor_transform() == transform
        assert (house.stair_step_count > 0) == house.get_editor_property('stairs').get_editor_property('enabled')
        assert house.house_mesh.get_dynamic_mesh().get_is_closed_mesh()
        report.append({'level':level,'actor':house.get_actor_label(),
            'path':house.get_path_name(),'triangles':house.triangle_count,
            'steps':house.stair_step_count,'stair_height':house.generated_stair_height,
            'transform':str(transform)})
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, level)

# This is the only existing external-actor package that owns a fantasy house.
allowed_actor = '__ExternalActors__\\FirstPerson\\Lvl_FirstPerson\\4\\RD\\KA2SUH9C0GCP6ZLW7YG65R.uasset'
unrelated_changes = []
for relative, expected in before.items():
    if not relative.startswith('__ExternalActors__') or relative == allowed_actor: continue
    path = content / relative
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        shutil.copy2(backup / relative, path)
        unrelated_changes.append(relative)
assert not unrelated_changes, 'Unrelated external actors were restored: ' + str(unrelated_changes)
(out / 'OpenWindowsStairsSaved.json').write_text(json.dumps({'actors':report,
    'blueprint':bp.get_path_name(),'backup':str(backup),'unrelated_actor_packages_unchanged':True},indent=2))
unreal.log('FANTASY_HOUSE_OPEN_WINDOWS_STAIRS_SAVED ' + json.dumps(report))
