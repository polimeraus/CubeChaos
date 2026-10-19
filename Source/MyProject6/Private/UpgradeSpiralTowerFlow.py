"""Persist the requested thin vertical strip on class defaults and placed towers."""
import json
import runpy
import shutil
from pathlib import Path
import unreal

content = Path(unreal.Paths.project_content_dir()).resolve()
repair_start_only = globals().get('REPAIR_START_ONLY', False)
out = Path(unreal.Paths.project_saved_dir()).resolve() / 'SpiralTower' / ('GlowStart' if repair_start_only else 'VerticalStrip')
out.mkdir(parents=True, exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cls = unreal.ChunkedDestructibleSpiralTower


def protect(file):
    if file.exists():
        target = out / 'Before/Content' / file.relative_to(content)
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)


def identity(actor):
    p, r, s = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
    return dict(transform=[p.x, p.y, p.z, r.pitch, r.yaw, r.roll, s.x, s.y, s.z],
                label=actor.get_actor_label(), folder=str(actor.get_folder_path()))


def settings(actor):
    return {key: actor.get_editor_property(key) for key in (
        'glow_band_height', 'glow_speed', 'glow_pause', 'loop_glow', 'glow_brightness',
        'flowing_glow', 'glow_preview_time', 'foundation')}


def update_width(actor):
    if repair_start_only:
        return
    # Reconcile old broad footprints, including the placed tower's 24800 cm
    # override, so the requested line appearance is also visible in saved maps.
    actor.set_editor_property('glow_trail_length', 600.)


protect(content / 'Structures/Destructible/SpiralTower/Materials/M_SpiralTowerFlowGlow.uasset')
if not repair_start_only:
    runpy.run_path(str(Path(__file__).with_name('SpiralTowerFlowMaterial.py')))['create_material'](True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
blueprints = []
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'Blueprint'), True):
    parent = str(asset.get_tag_value('NativeParentClass') or asset.get_tag_value('ParentClass') or '')
    if cls.__name__ not in parent:
        continue
    bp = asset.get_asset()
    cdo = unreal.get_default_object(bp.generated_class())
    if not isinstance(cdo, cls):
        continue
    protect(content / (bp.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset'))
    bp.modify()
    before = settings(cdo)
    update_width(cdo)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert settings(unreal.get_default_object(bp.generated_class())) == before
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    blueprints.append(bp.get_path_name())

names = [cls.__name__.encode()] + [p.split('.')[-1].encode() for p in blueprints]
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
    towers = [a for a in actors.get_all_level_actors() if isinstance(a, cls)]
    assert towers or not matches, ('Target actors were not loaded', path)
    for tower in towers:
        before, options = identity(tower), settings(tower)
        original_width = tower.glow_trail_length
        tower.modify()
        update_width(tower)
        tower.rebuild_tower()
        assert not tower.generation_warning, tower.generation_warning
        assert identity(tower) == before and settings(tower) == options
        assert not repair_start_only or tower.glow_trail_length == original_width
        assert tower.generated_glow_path_length > tower.generated_height * 3
        records.append(dict(map=path, cls=tower.get_class().get_path_name(), **before,
                            settings=options, trail=tower.glow_trail_length,
                            path_length=tower.generated_glow_path_length, pieces=tower.generated_piece_count))
    if towers:
        assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
        assert unreal.EditorLoadingAndSavingUtils.save_packages([a.get_package() for a in towers], False)
assert blueprints and len(records) >= 2
(out / 'SavedInstances.json').write_text(json.dumps(dict(blueprints=blueprints, instances=records), indent=2))
unreal.log('SPIRAL_STAIR_FLOW_SAVED ' + json.dumps(records))
