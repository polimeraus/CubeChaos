"""Read-only baseline for the Content Browser organization migration."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'ContentOrganization'
out.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
    include_hard_package_references=True, include_searchable_names=False,
    include_soft_management_references=False, include_hard_management_references=False)
assets = []
for asset in registry.get_assets_by_path('/Game', True):
    row = dict(package=str(asset.package_name), name=str(asset.asset_name),
               type=str(asset.asset_class_path.asset_name))
    if row['type'] in ('Blueprint', 'AnimBlueprint'):
        row['parent'] = str(asset.get_tag_value('ParentClass') or '')
        row['native_parent'] = str(asset.get_tag_value('NativeParentClass') or '')
    row['dependencies'] = [str(d) for d in registry.get_dependencies(asset.package_name, options)
                           if str(d).startswith('/Game/')]
    assets.append(row)
(out / 'BaselineAssets.json').write_text(json.dumps(assets, indent=2), encoding='utf-8')
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/FirstPerson/Lvl_FirstPerson')
assert world
actors = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    row = dict(name=actor.get_name(), label=actor.get_actor_label(),
               actor_class=actor.get_class().get_path_name(), folder=str(actor.get_folder_path()),
               transform=actor.get_actor_transform().to_tuple())
    if isinstance(actor, (unreal.ChasingNPC, unreal.WhisperFollower)):
        mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
        row['mesh'] = mesh.get_editor_property('skeletal_mesh_asset').get_path_name() if mesh.get_editor_property('skeletal_mesh_asset') else None
        row['mesh_relative'] = str(mesh.get_relative_transform())
    actors.append(row)
(out / 'BaselineActors.json').write_text(json.dumps(actors, indent=2, default=str), encoding='utf-8')
unreal.log('CONTENT_BASELINE_OK assets=%s actors=%s' % (len(assets), len(actors)))
