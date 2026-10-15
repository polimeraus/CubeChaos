"""Final read-only asset-registry and compatibility check in a fresh commandlet."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'CharacterMigration'
content = Path(unreal.Paths.project_content_dir()).resolve()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
packs = ('ParagonCrunch', 'ParagonFengMao', 'ParagonGRIMexe')
original = json.loads((out / 'Audit.json').read_text())
baseline = json.loads((out / 'SourceDetails.json').read_text())['missing_before']
npcs = json.loads((out / 'NPCs.json').read_text())['npcs']


def moved(path):
    for pack in packs:
        if path.startswith('/Game/' + pack + '/'):
            return path.replace('/Game/', '/Game/NPC/', 1)
    return path


def exists(package):
    return any((content / (package[6:] + ext)).is_file() for ext in ('.uasset', '.umap'))


report = {'passed': False, 'package_counts': {}, 'new_missing_dependencies': {}, 'root_motion': {}, 'old_path_loads': []}
for pack in packs:
    assert not (content / pack).exists()
    old_assets = registry.get_assets_by_path('/Game/' + pack, recursive=True)
    assert not old_assets, 'Asset registry still exposes original root: ' + pack
    report['package_counts'][pack] = len(registry.get_assets_by_path('/Game/NPC/' + pack, recursive=True))
for package, dependencies in original['dependencies'].items():
    assert exists(moved(package)), package
    missing = [d for d in dependencies if d.startswith('/Game/') and not exists(moved(d)) and d not in baseline.get(package, [])]
    if missing:
        report['new_missing_dependencies'][package] = missing
assert not report['new_missing_dependencies']
for entry in npcs:
    bp = unreal.load_asset(entry['blueprint'])
    defaults = unreal.get_default_object(bp.generated_class())
    rotation = defaults.mesh.get_editor_property('relative_rotation')
    unreal.log('NPC_MESH_ROTATION ' + bp.get_name() + ' ' + str(rotation))
    assert abs(rotation.pitch) < .01 and abs(rotation.roll) < .01, str(rotation)
    mesh = defaults.mesh.get_skeletal_mesh_asset()
    assert mesh.get_path_name() == entry['mesh']
    old = entry['mesh'].replace('/Game/NPC/', '/Game/', 1)
    assert unreal.load_object(None, old) == mesh, old
    report['old_path_loads'].append(old)
    if entry['base']:
        motions = {}
        ext = unreal.AnimPoseExtensions
        for key, path in entry['animations'].items():
            animation = unreal.load_asset(path)
            options = unreal.AnimPoseEvaluationOptions()
            options.set_editor_property('extract_root_motion', False)
            first = ext.get_anim_pose_at_time(animation, 0., options)
            last = ext.get_anim_pose_at_time(animation, animation.get_play_length(), options)
            first_root = ext.get_bone_pose(first, 'root', unreal.AnimPoseSpaces.WORLD).translation
            last_root = ext.get_bone_pose(last, 'root', unreal.AnimPoseSpaces.WORLD).translation
            motions[key] = {'root_delta_cm': (last_root-first_root).length(),
                            'force_root_lock': animation.get_editor_property('force_root_lock'),
                            'enable_root_motion': animation.get_editor_property('enable_root_motion')}
        report['root_motion'][entry['spec']['name']] = motions
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
report['main_map_loads'] = True
report['main_map_actor_count'] = len(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
report['preexisting_missing_dependency_owners'] = len(baseline)
report['passed'] = True
(out / 'ContentVerification.json').write_text(json.dumps(report, indent=2))
unreal.log('PARAGON_CONTENT_VERIFIED')
