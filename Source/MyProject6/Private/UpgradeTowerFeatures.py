"""Persist the requested tower defaults and reconcile saved instances without copying level identity."""
import json
import runpy
import shutil
from pathlib import Path
import unreal

content = Path(unreal.Paths.project_content_dir()).resolve()
out = Path(unreal.Paths.project_saved_dir()).resolve() / 'TowerFeatures'
out.mkdir(parents=True, exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def protect(file):
    if file.exists():
        target = out / 'Before/Content' / file.relative_to(content)
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)


def pose(actor):
    p, r, s = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
    return [p.x, p.y, p.z, r.pitch, r.yaw, r.roll, s.x, s.y, s.z]


material = runpy.run_path(str(Path(__file__).with_name('SpiralTowerFlowMaterial.py')))['create_material']()
spiral = unreal.get_default_object(unreal.ChunkedDestructibleSpiralTower)
spiral.set_editor_property('glow_material', material)
specs = {
    'Babylon': (unreal.ChunkedDestructibleBabylonTower, ('scale_windows_with_tiers', 'window_height_ratio')),
    'Spiral': (unreal.ChunkedDestructibleSpiralTower, ('foundation', 'traversal', 'flowing_glow', 'glow_material',
        'glow_band_height', 'glow_speed', 'glow_pause', 'loop_glow', 'glow_brightness', 'glow_color', 'glow_preview_time')),
}


def kind(actor):
    return next((key for key, (cls, _) in specs.items() if isinstance(actor, cls)), None)


def configure(actor):
    key = kind(actor)
    cls, properties = specs[key]
    native = unreal.get_default_object(cls)
    actor.modify()
    for prop in properties:
        actor.set_editor_property(prop, native.get_editor_property(prop))
    return key


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
blueprints = []
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'Blueprint'), True):
    parent = str(asset.get_tag_value('NativeParentClass') or asset.get_tag_value('ParentClass') or '')
    if not any(cls.__name__ in parent for cls, _ in specs.values()):
        continue
    bp = asset.get_asset()
    cdo = unreal.get_default_object(bp.generated_class())
    if not kind(cdo):
        continue
    protect(content / (bp.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset'))
    key = configure(cdo)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    blueprints.append(dict(path=bp.get_path_name(), kind=key))
assert {row['kind'] for row in blueprints} == set(specs)

names = [cls.__name__.encode() for cls, _ in specs.values()]
names += [row['path'].split('.')[-1].encode() for row in blueprints]
records = []
for file in sorted(content.rglob('*.umap')):
    external = content / '__ExternalActors__' / file.relative_to(content).with_suffix('')
    matches = [p for p in external.rglob('*.uasset') if any(n in p.read_bytes() for n in names)] if external.exists() else []
    if not matches and not any(n in file.read_bytes() for n in names):
        continue
    path = '/Game/' + file.relative_to(content).with_suffix('').as_posix()
    protect(file)
    for package in matches:
        protect(package)
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    assert world, path
    targets = [actor for actor in actors.get_all_level_actors() if kind(actor)]
    assert targets or not matches, ('Target actors were not loaded', path)
    for actor in targets:
        before = pose(actor)
        label, folder = actor.get_actor_label(), str(actor.get_folder_path())
        key = configure(actor)
        actor.rebuild_tower()
        assert not actor.generation_warning, (label, actor.generation_warning)
        assert pose(actor) == before and actor.get_actor_label() == label and str(actor.get_folder_path()) == folder
        records.append(dict(map=path, label=label, cls=actor.get_class().get_path_name(), kind=key,
                            transform=before, folder=folder, pieces=actor.generated_piece_count,
                            openings=actor.generated_opening_count if key == 'Babylon' else 0,
                            height=actor.generated_height))
    if targets:
        assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
        assert unreal.EditorLoadingAndSavingUtils.save_packages([actor.get_package() for actor in targets], False)
assert {row['kind'] for row in records} == set(specs)
(out / 'SavedInstances.json').write_text(json.dumps(dict(
    keys={key: list(props) for key, (_, props) in specs.items()}, blueprints=blueprints, instances=records), indent=2))
unreal.log('TOWER_FEATURES_SAVED ' + json.dumps(records))
